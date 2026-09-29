#include "ltdc-graphics.h"

#include "board.h"

namespace {

// Rechteck r um e erweitern; ein leeres r wird zu e.
void unite(Common::Rect &r, const Common::Rect &e) {
  if (e.isEmpty()) return;
  if (r.isEmpty()) r = e;
  else r.extend(e);
}

// Rechteck nach außen auf Vielfache von n ausrichten (Blockkopie).
Common::Rect alignOut(const Common::Rect &r, int n) {
  return Common::Rect(r.left & ~(n - 1), r.top & ~(n - 1), (r.right + n - 1) & ~(n - 1),
                      (r.bottom + n - 1) & ~(n - 1));
}

uint16 toRgb565(const byte *c) {
  return ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3);
}

}  // namespace

LtdcGraphicsManager::LtdcGraphicsManager(uint8 *layer0, uint16 *layer1)
    : _layer0(layer0), _layer1(layer1), _overlay(new uint16[kOverlayWidth * kOverlayHeight]()) {}

LtdcGraphicsManager::~LtdcGraphicsManager() {
  delete[] _screen;
  delete[] _overlay;
  delete[] _cursor;
}

// --- Koordinaten -------------------------------------------------------------

Common::Point LtdcGraphicsManager::fromPanel(int px, int py) const {
  int x, y, w, h;
  if (_overlayVisible) {
    x = py;
    y = 239 - px;
    w = kOverlayWidth;
    h = kOverlayHeight;
  } else {
    x = py - gameY0();
    y = gameX0() + _height - 1 - px;
    w = _width;
    h = _height;
  }
  if (w == 0) return Common::Point();  // noch kein Spielbild
  return Common::Point(CLIP(x, 0, w - 1), CLIP(y, 0, h - 1));
}

void LtdcGraphicsManager::setMousePos(const Common::Point &pos) {
  if (pos == _mouse) return;
  _mouse = pos;
  _cursorChanged = true;
}

// --- Spielbild ---------------------------------------------------------------

void LtdcGraphicsManager::beginGFXTransaction() {
  _transactionFailed = false;
  _pendingWidth = _width;
  _pendingHeight = _height;
}

void LtdcGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
  // Das gedrehte Bild muss aufs Panel passen; die Blockkopie braucht
  // Kantenlängen in Vielfachen von 4.
  if (width > 320 || height > 240 || (width & 3) || (height & 3) ||
      (format && *format != Graphics::PixelFormat::createFormatCLUT8())) {
    _transactionFailed = true;
    return;
  }
  _pendingWidth = width;
  _pendingHeight = height;
}

OSystem::TransactionError LtdcGraphicsManager::endGFXTransaction() {
  if (_transactionFailed) return OSystem::kTransactionSizeChangeFailed;
  if (_pendingWidth == _width && _pendingHeight == _height) return OSystem::kTransactionSuccess;

  _width = _pendingWidth;
  _height = _pendingHeight;
  delete[] _screen;
  _screen = new uint8[_width * _height]();
  _screenSurface.init(_width, _height, _width, _screen, Graphics::PixelFormat::createFormatCLUT8());
  memset(_layer0, 0, _width * _height);
  _cursorOnScreen = Common::Rect();
  _screenDirty = screenRect();
  _shakeDirty = true;  // setzt auch die Lage der Ebene für die neue Größe
  _screenChangeID++;
  return OSystem::kTransactionSuccess;
}

void LtdcGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
  if (start >= 256) return;
  num = MIN<uint>(num, 256 - start);
  memcpy(_palette + 3 * start, colors, 3 * num);
  for (uint i = start; i < start + num; ++i) _palette565[i] = toRgb565(_palette + 3 * i);
  _paletteDirtyStart = MIN(_paletteDirtyStart, start);
  _paletteDirtyEnd = MAX(_paletteDirtyEnd, start + num);
  // Der Zeiger im Menübild ist mit Spielfarben gemalt.
  if (_overlayVisible && _mouseVisible) _cursorChanged = true;
}

void LtdcGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
  if (start >= 256) return;
  memcpy(colors, _palette + 3 * start, 3 * MIN<uint>(num, 256 - start));
}

void LtdcGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
  Common::Rect r(x, y, x + w, y + h);
  r.clip(screenRect());
  if (r.isEmpty()) return;
  const uint8 *src = (const uint8 *)buf + (r.top - y) * pitch + (r.left - x);
  for (int row = r.top; row < r.bottom; ++row, src += pitch)
    memcpy(_screen + row * _width + r.left, src, r.width());
  markScreenDirty(r);
}

Graphics::Surface *LtdcGraphicsManager::lockScreen() {
  return &_screenSurface;
}

void LtdcGraphicsManager::unlockScreen() {
  markScreenDirty(screenRect());
}

void LtdcGraphicsManager::fillScreen(uint32 col) {
  memset(_screen, (uint8)col, _width * _height);
  markScreenDirty(screenRect());
}

void LtdcGraphicsManager::fillScreen(const Common::Rect &area, uint32 col) {
  Common::Rect r = area;
  r.clip(screenRect());
  for (int row = r.top; row < r.bottom; ++row) memset(_screen + row * _width + r.left, (uint8)col, r.width());
  markScreenDirty(r);
}

void LtdcGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
  if (shakeXOffset == _shakeX && shakeYOffset == _shakeY) return;
  _shakeX = shakeXOffset;
  _shakeY = shakeYOffset;
  _shakeDirty = true;
}

void LtdcGraphicsManager::markScreenDirty(const Common::Rect &r) {
  unite(_screenDirty, r);
}

// Spielbild-Ausschnitt gedreht in Ebene 0 kopieren: je 4×4 Pixel mit vier
// Wortzugriffen lesen, im Register transponieren, vier Wörter schreiben.
// Spielpixel (x, y) → Bildspeicher Zeile x, Spalte H−1−y.
void LtdcGraphicsManager::blitScreen(Common::Rect r) {
  r = alignOut(r, 4);
  r.clip(screenRect());
  const int w = _width, h = _height;
  for (int y = r.top; y < r.bottom; y += 4) {
    const uint32 *s0 = (const uint32 *)(_screen + y * w);
    const uint32 *s1 = (const uint32 *)(_screen + (y + 1) * w);
    const uint32 *s2 = (const uint32 *)(_screen + (y + 2) * w);
    const uint32 *s3 = (const uint32 *)(_screen + (y + 3) * w);
    for (int x = r.left; x < r.right; x += 4) {
      const uint32 a = s0[x / 4], b = s1[x / 4], c = s2[x / 4], d = s3[x / 4];
      // Zielspalten h−4−y … h−1−y: Byte k des Zielworts stammt aus Zeile y+3−k.
      uint32 *dst = (uint32 *)(_layer0 + x * h + (h - 4 - y));
      const int stride = h / 4;
      for (int i = 0; i < 4; ++i, dst += stride) {
        const int sh = 8 * i;
        *dst = ((d >> sh) & 0xFF) | (((c >> sh) & 0xFF) << 8) | (((b >> sh) & 0xFF) << 16) | (((a >> sh) & 0xFF) << 24);
      }
    }
  }
}

void LtdcGraphicsManager::drawCursorOnScreen(const Common::Rect &r) {
  const int left = _mouse.x - _hotspotX, top = _mouse.y - _hotspotY;
  for (int y = r.top; y < r.bottom; ++y) {
    const uint8 *src = _cursor + (y - top) * _cursorW + (r.left - left);
    for (int x = r.left; x < r.right; ++x, ++src) {
      if (!_keyed || *src != _keycolor) _layer0[x * _height + (_height - 1 - y)] = *src;
    }
  }
}

// --- Menüebene -------------------------------------------------------------

Graphics::PixelFormat LtdcGraphicsManager::getOverlayFormat() const {
  return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
}

void LtdcGraphicsManager::showOverlay(bool inGUI) {
  if (_overlayVisible) return;
  // Zeiger bleibt an derselben Stelle des Panels: Spiel- → Menükoordinaten.
  const int px = gameX0() + _height - 1 - _mouse.y, py = gameY0() + _mouse.x;
  _overlayVisible = true;
  _mouse = fromPanel(px, py);
  _overlayDirty = overlayRect();
  _cursorChanged = true;
}

void LtdcGraphicsManager::hideOverlay() {
  if (!_overlayVisible) return;
  const int px = 239 - _mouse.y, py = _mouse.x;
  _overlayVisible = false;
  _mouse = fromPanel(px, py);
  // Zeiger kann auf dem Spielbild inzwischen woanders stehen.
  _screenDirty = screenRect();
  _cursorChanged = true;
}

// Menübild = abgedunkeltes Spielbild übernimmt die GUI; hier die Vorlage:
// Spielbild in RGB565 an seiner Panelposition, Rand schwarz.
void LtdcGraphicsManager::clearOverlay() {
  memset(_overlay, 0, kOverlayWidth * kOverlayHeight * sizeof(uint16));
  // Spielpixel (x, y) liegt an Panel (x0 + H−1−y, y0 + x) = Menüpixel (y0 + x, 239 − x0 − H + 1 + y).
  const int ox = gameY0(), oy = 240 - gameX0() - _height;
  for (int y = 0; y < _height; ++y) {
    const uint8 *src = _screen + y * _width;
    uint16 *dst = _overlay + (oy + y) * kOverlayWidth + ox;
    for (int x = 0; x < _width; ++x) dst[x] = _palette565[src[x]];
  }
  markOverlayDirty(overlayRect());
}

void LtdcGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
  const int w = MIN<int>(surface.w, kOverlayWidth), h = MIN<int>(surface.h, kOverlayHeight);
  for (int y = 0; y < h; ++y)
    memcpy((byte *)surface.getBasePtr(0, y), _overlay + y * kOverlayWidth, w * sizeof(uint16));
}

void LtdcGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
  Common::Rect r(x, y, x + w, y + h);
  r.clip(overlayRect());
  if (r.isEmpty()) return;
  const byte *src = (const byte *)buf + (r.top - y) * pitch + (r.left - x) * sizeof(uint16);
  for (int row = r.top; row < r.bottom; ++row, src += pitch)
    memcpy(_overlay + row * kOverlayWidth + r.left, src, r.width() * sizeof(uint16));
  markOverlayDirty(r);
}

void LtdcGraphicsManager::markOverlayDirty(const Common::Rect &r) {
  unite(_overlayDirty, r);
}

// Menübild-Ausschnitt gedreht in Ebene 1: je 2×2 Pixel über Wortzugriffe.
// Menüpixel (x, y) → Bildspeicher Zeile x, Spalte 239−y.
void LtdcGraphicsManager::blitOverlay(Common::Rect r) {
  r = alignOut(r, 2);
  r.clip(overlayRect());
  for (int y = r.top; y < r.bottom; y += 2) {
    const uint32 *p = (const uint32 *)(_overlay + y * kOverlayWidth);
    const uint32 *q = (const uint32 *)(_overlay + (y + 1) * kOverlayWidth);
    for (int x = r.left; x < r.right; x += 2) {
      const uint32 a = p[x / 2], b = q[x / 2];
      // Zielspalten 238−y (aus Zeile y+1) und 239−y (aus Zeile y).
      uint32 *dst = (uint32 *)(_layer1 + x * 240 + (238 - y));
      dst[0] = (b & 0xFFFF) | (a << 16);
      dst[240 / 2] = (b >> 16) | (a & 0xFFFF0000);
    }
  }
}

void LtdcGraphicsManager::drawCursorOnOverlay(const Common::Rect &r) {
  const int left = _mouse.x - _hotspotX, top = _mouse.y - _hotspotY;
  for (int y = r.top; y < r.bottom; ++y) {
    const uint8 *src = _cursor + (y - top) * _cursorW + (r.left - left);
    for (int x = r.left; x < r.right; ++x, ++src) {
      if (!_keyed || *src != _keycolor) _layer1[x * 240 + (239 - y)] = _palette565[*src];
    }
  }
}

// --- Mauszeiger ------------------------------------------------------------

Common::Rect LtdcGraphicsManager::cursorRect() const {
  if (!_mouseVisible || !_cursor) return Common::Rect();
  Common::Rect r(_mouse.x - _hotspotX, _mouse.y - _hotspotY, _mouse.x - _hotspotX + _cursorW,
                 _mouse.y - _hotspotY + _cursorH);
  r.clip(_overlayVisible ? overlayRect() : screenRect());
  return r;
}

bool LtdcGraphicsManager::showMouse(bool visible) {
  const bool last = _mouseVisible;
  if (visible != _mouseVisible) {
    _mouseVisible = visible;
    _cursorChanged = true;
  }
  return last;
}

void LtdcGraphicsManager::warpMouse(int x, int y) {
  setMousePos(Common::Point(x, y));
}

void LtdcGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
                                         uint32 keycolor, bool dontScale, const Graphics::PixelFormat *format,
                                         const byte *mask) {
  // Ohne USE_RGB_COLOR kommen nur CLUT8-Zeiger; skaliert wird nie.
  delete[] _cursor;
  _cursor = new uint8[w * h];
  memcpy(_cursor, buf, w * h);
  _keyed = keycolor < 256;
  _keycolor = (uint8)keycolor;
  if (mask) {
    // Die Maske ersetzt die Schlüsselfarbe: transparente Pixel bekommen einen
    // Index, den kein deckendes Pixel nutzt. Gibt es keinen, bleibt der
    // Zeiger deckend. (Invertier-Modi kann der LTDC ohnehin nicht.)
    bool used[256] = {};
    for (uint i = 0; i < w * h; ++i) {
      if (mask[i] != kCursorMaskTransparent) used[_cursor[i]] = true;
    }
    int unused = 0;
    while (unused < 256 && used[unused]) ++unused;
    _keyed = unused < 256;
    _keycolor = (uint8)unused;
    for (uint i = 0; _keyed && i < w * h; ++i) {
      if (mask[i] == kCursorMaskTransparent) _cursor[i] = _keycolor;
    }
  }
  _cursorW = w;
  _cursorH = h;
  _hotspotX = hotspotX;
  _hotspotY = hotspotY;
  _cursorChanged = true;
}

// --- Bild aufbauen -----------------------------------------------------------

void LtdcGraphicsManager::updateScreen() {
  if (_paletteDirtyEnd > _paletteDirtyStart) {
    lcd_set_palette(_palette + 3 * _paletteDirtyStart, _paletteDirtyStart, _paletteDirtyEnd - _paletteDirtyStart);
    _paletteDirtyStart = 256;
    _paletteDirtyEnd = 0;
  }
  if (_shakeDirty && _width) {
    // Wackeln verschiebt die Ebene statt das Bild neu zu kopieren: Spielbild
    // um (sx, sy) versetzt = Panel um (−sy, sx).
    lcd_place_layer0(gameX0() - _shakeY, gameY0() + _shakeX, _height, _width);
    _shakeDirty = false;
  }

  const Common::Rect cursor = cursorRect();
  if (_overlayVisible) {
    if (_cursorChanged) {
      unite(_overlayDirty, _cursorOnOverlay);
      unite(_overlayDirty, cursor);
    }
    if (!_overlayDirty.isEmpty()) {
      blitOverlay(_overlayDirty);
      Common::Rect c = cursor;
      c.clip(alignOut(_overlayDirty, 2));
      if (!c.isEmpty()) drawCursorOnOverlay(c);
      _cursorOnOverlay = cursor;
      _overlayDirty = Common::Rect();
    }
  } else if (_width) {
    if (_cursorChanged) {
      unite(_screenDirty, _cursorOnScreen);
      unite(_screenDirty, cursor);
    }
    if (!_screenDirty.isEmpty()) {
      blitScreen(_screenDirty);
      Common::Rect c = cursor;
      c.clip(alignOut(_screenDirty, 4));
      if (!c.isEmpty()) drawCursorOnScreen(c);
      _cursorOnScreen = cursor;
      _screenDirty = Common::Rect();
    }
  }
  _cursorChanged = false;

  if (_overlayShown != _overlayVisible) {
    lcd_show_overlay(_overlayVisible);
    _overlayShown = _overlayVisible;
  }
}
