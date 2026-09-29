// ScummVM-Grafik über den LTDC des STM32F429.
//
// Das Panel steht hochkant (240×320), gespielt wird quer: das Board liegt
// mit der ST-LINK-Buchse nach links. Beide Bilder werden deshalb um 90°
// gedreht in die Bildspeicher der LTDC-Ebenen kopiert:
//   Ebene 0 (L8, CLUT)   Spielbild W×H, mittig; Pixel (x, y) liegt an
//                        Panelposition (x0 + H−1−y, y0 + x)
//   Ebene 1 (RGB565)     ScummVM-Menüs, 320×240 über das ganze Panel;
//                        Pixel (x, y) liegt an (239−y, x)
// Der Mauszeiger wird beim Kopieren ins Bild der sichtbaren Ebene gemalt.
#pragma once

#include "backends/graphics/graphics.h"
#include "common/rect.h"
#include "graphics/surface.h"

class LtdcGraphicsManager : public GraphicsManager {
public:
  static constexpr int kOverlayWidth = 320;
  static constexpr int kOverlayHeight = 240;

  // layer0: Bildspeicher für mindestens 320×240 Byte; layer1: 240×320 RGB565.
  LtdcGraphicsManager(uint8 *layer0, uint16 *layer1);
  ~LtdcGraphicsManager() override;

  // Panelpunkt in Koordinaten des gerade sichtbaren Bildes (Spiel oder
  // Menü), auf das Bild beschnitten.
  Common::Point fromPanel(int px, int py) const;
  // Zeigerposition setzen, ohne dass ScummVM ein Ereignis erwartet
  // (Touch-Eingabe; warpMouse ist für Engine und GUI).
  void setMousePos(const Common::Point &pos);

  bool hasFeature(OSystem::Feature f) const override { return false; }
  void setFeatureState(OSystem::Feature f, bool enable) override {}
  bool getFeatureState(OSystem::Feature f) const override { return false; }

  void initSize(uint width, uint height, const Graphics::PixelFormat *format) override;
  int getScreenChangeID() const override { return _screenChangeID; }
  void beginGFXTransaction() override;
  OSystem::TransactionError endGFXTransaction() override;

  int16 getHeight() const override { return _height; }
  int16 getWidth() const override { return _width; }
  void setPalette(const byte *colors, uint start, uint num) override;
  void grabPalette(byte *colors, uint start, uint num) const override;
  void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
  Graphics::Surface *lockScreen() override;
  void unlockScreen() override;
  void fillScreen(uint32 col) override;
  void fillScreen(const Common::Rect &r, uint32 col) override;
  void updateScreen() override;
  void setShakePos(int shakeXOffset, int shakeYOffset) override;
  void setFocusRectangle(const Common::Rect &rect) override {}
  void clearFocusRectangle() override {}

  void showOverlay(bool inGUI) override;
  void hideOverlay() override;
  bool isOverlayVisible() const override { return _overlayVisible; }
  Graphics::PixelFormat getOverlayFormat() const override;
  void clearOverlay() override;
  void grabOverlay(Graphics::Surface &surface) const override;
  void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
  int16 getOverlayHeight() const override { return kOverlayHeight; }
  int16 getOverlayWidth() const override { return kOverlayWidth; }

  bool showMouse(bool visible) override;
  void warpMouse(int x, int y) override;
  void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
                      bool dontScale, const Graphics::PixelFormat *format, const byte *mask) override;
  void setCursorPalette(const byte *colors, uint start, uint num) override {}

private:
  // Panelposition der linken oberen Ecke des gedrehten Spielbilds (ohne Wackeln).
  int gameX0() const { return (240 - _height) / 2; }
  int gameY0() const { return (320 - _width) / 2; }
  Common::Rect screenRect() const { return Common::Rect(_width, _height); }
  Common::Rect overlayRect() const { return Common::Rect(kOverlayWidth, kOverlayHeight); }
  Common::Rect cursorRect() const;
  void markScreenDirty(const Common::Rect &r);
  void markOverlayDirty(const Common::Rect &r);
  void blitScreen(Common::Rect r);
  void blitOverlay(Common::Rect r);
  void drawCursorOnScreen(const Common::Rect &r);
  void drawCursorOnOverlay(const Common::Rect &r);

  uint8 *const _layer0;
  uint16 *const _layer1;

  int _width = 0, _height = 0;
  int _pendingWidth = 0, _pendingHeight = 0;
  bool _transactionFailed = false;
  int _screenChangeID = 0;
  uint8 *_screen = nullptr;  // Spielbild ungedreht, W×H
  Graphics::Surface _screenSurface;
  Common::Rect _screenDirty;

  byte _palette[256 * 3] = {};
  uint16 _palette565[256] = {};
  uint _paletteDirtyStart = 256, _paletteDirtyEnd = 0;

  int _shakeX = 0, _shakeY = 0;
  bool _shakeDirty = false;

  uint16 *_overlay;  // Menübild ungedreht, 320×240
  Common::Rect _overlayDirty;
  bool _overlayVisible = false;  // Wunsch von ScummVM
  bool _overlayShown = false;    // Stand am Display

  Common::Point _mouse;  // in Koordinaten des sichtbaren Bildes
  bool _mouseVisible = false;
  uint8 *_cursor = nullptr;
  int _cursorW = 0, _cursorH = 0, _hotspotX = 0, _hotspotY = 0;
  uint8 _keycolor = 0;
  bool _keyed = false;  // false: Zeiger ohne transparente Pixel
  Common::Rect _cursorOnScreen, _cursorOnOverlay;  // wo der Zeiger zuletzt gemalt wurde
  bool _cursorChanged = false;
};
