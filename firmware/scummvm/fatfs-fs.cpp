#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "fatfs-fs.h"

#include "backends/fs/abstract-fs.h"
#include "common/bufferedstream.h"
#include "common/stream.h"

#include "ff.h"

namespace {

// Lesepuffer vor FatFs: ScummVM liest oft byteweise; jeder FatFs-Aufruf
// kostet mehr als ein Kopieren aus dem Puffer.
constexpr uint32 kReadBufferSize = 4096;

Common::String fatPath(const Common::String &path) {
  return "0:" + path;
}

Common::String joinPath(const Common::String &dir, const Common::String &name) {
  return dir == "/" ? "/" + name : dir + "/" + name;
}

// Pfad vereinheitlichen: führender „/“, kein abschließender (außer Wurzel).
Common::String normalize(Common::String path) {
  if (path.empty() || path[0] != '/') path = "/" + path;
  while (path.size() > 1 && path.lastChar() == '/') path.deleteLastChar();
  return path;
}

class FatFsReadStream : public Common::SeekableReadStream {
public:
  bool open(const Common::String &path) {
    _open = f_open(&_file, fatPath(path).c_str(), FA_READ) == FR_OK;
    return _open;
  }
  ~FatFsReadStream() override {
    if (_open) f_close(&_file);
  }

  uint32 read(void *buf, uint32 len) override {
    UINT got = 0;
    if (f_read(&_file, buf, len, &got) != FR_OK) {
      _err = true;
      return 0;
    }
    if (got < len) _eos = true;
    return got;
  }
  bool eos() const override { return _eos; }
  bool err() const override { return _err; }
  void clearErr() override {
    _err = false;
    _eos = false;
  }
  int64 pos() const override { return f_tell(&_file); }
  int64 size() const override { return f_size(&_file); }
  bool seek(int64 offset, int whence) override {
    int64 target = whence == SEEK_SET ? offset : whence == SEEK_CUR ? pos() + offset : size() + offset;
    if (target < 0 || target > size()) {
      _err = true;
      return false;
    }
    _eos = false;
    return f_lseek(&_file, (FSIZE_t)target) == FR_OK;
  }

private:
  FIL _file;
  bool _open = false;
  bool _eos = false;
  bool _err = false;
};

// Schreibt bei „atomic“ erst in „name.tmp“ und benennt beim Abschluss um, so
// bleibt ein alter Spielstand erhalten, falls das Schreiben scheitert.
class FatFsWriteStream : public Common::SeekableWriteStream {
public:
  bool open(const Common::String &path, bool atomic) {
    _path = path;
    _atomic = atomic;
    _target = atomic ? path + ".tmp" : path;
    _open = f_open(&_file, fatPath(_target).c_str(), FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
    return _open;
  }
  ~FatFsWriteStream() override { finalize(); }

  uint32 write(const void *buf, uint32 len) override {
    UINT put = 0;
    if (f_write(&_file, buf, len, &put) != FR_OK || put != len) _err = true;
    return put;
  }
  bool flush() override { return f_sync(&_file) == FR_OK; }
  void finalize() override {
    if (!_open) return;
    _open = false;
    if (f_close(&_file) != FR_OK) _err = true;
    if (_atomic) {
      if (_err) {
        f_unlink(fatPath(_target).c_str());
      } else {
        f_unlink(fatPath(_path).c_str());
        if (f_rename(fatPath(_target).c_str(), fatPath(_path).c_str()) != FR_OK) _err = true;
      }
    }
  }
  bool err() const override { return _err; }
  void clearErr() override { _err = false; }
  int64 pos() const override { return f_tell(&_file); }
  int64 size() const override { return f_size(&_file); }
  bool seek(int64 offset, int whence) override {
    int64 target = whence == SEEK_SET ? offset : whence == SEEK_CUR ? pos() + offset : size() + offset;
    if (target < 0) return false;
    return f_lseek(&_file, (FSIZE_t)target) == FR_OK;
  }

private:
  FIL _file;
  Common::String _path, _target;
  bool _atomic = false;
  bool _open = false;
  bool _err = false;
};

class FatFsNode : public AbstractFSNode {
public:
  explicit FatFsNode(const Common::String &path) : _path(normalize(path)) {
    if (_path == "/") {
      _exists = _isDir = true;
      return;
    }
    FILINFO info;
    _exists = f_stat(fatPath(_path).c_str(), &info) == FR_OK;
    _isDir = _exists && (info.fattrib & AM_DIR);
  }
  FatFsNode(const Common::String &path, bool isDir) : _path(path), _isDir(isDir), _exists(true) {}

  bool exists() const override { return _exists; }
  Common::U32String getDisplayName() const override { return Common::U32String(getName()); }
  Common::String getName() const override {
    if (_path == "/") return "/";
    return Common::String(_path.c_str() + _path.findLastOf('/') + 1);
  }
  Common::String getPath() const override { return _path; }
  bool isDirectory() const override { return _isDir; }
  bool isReadable() const override { return _exists; }
  bool isWritable() const override { return true; }

  AbstractFSNode *getChild(const Common::String &name) const override {
    return new FatFsNode(joinPath(_path, name));
  }

  AbstractFSNode *getParent() const override {
    if (_path == "/") return nullptr;
    size_t slash = _path.findLastOf('/');
    return new FatFsNode(slash == 0 ? Common::String("/") : Common::String(_path.c_str(), slash));
  }

  bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override {
    if (!_isDir) return false;
    DIR dir;
    if (f_opendir(&dir, fatPath(_path).c_str()) != FR_OK) return false;
    FILINFO info;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
      bool isDir = info.fattrib & AM_DIR;
      // Punkt-Dateien sind versteckt – darunter auch die „._*“-Begleitdateien,
      // die macOS auf FAT-Sticks anlegt.
      bool isHidden = info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS));
      if (isHidden && !hidden) continue;
      if (mode == Common::FSNode::kListFilesOnly && isDir) continue;
      if (mode == Common::FSNode::kListDirectoriesOnly && !isDir) continue;
      list.push_back(new FatFsNode(joinPath(_path, info.fname), isDir));
    }
    f_closedir(&dir);
    return true;
  }

  Common::SeekableReadStream *createReadStream() override {
    if (!_exists || _isDir) return nullptr;
    FatFsReadStream *stream = new FatFsReadStream();
    if (!stream->open(_path)) {
      delete stream;
      return nullptr;
    }
    return Common::wrapBufferedSeekableReadStream(stream, kReadBufferSize, DisposeAfterUse::YES);
  }

  Common::SeekableWriteStream *createWriteStream(bool atomic) override {
    FatFsWriteStream *stream = new FatFsWriteStream();
    if (!stream->open(_path, atomic)) {
      delete stream;
      return nullptr;
    }
    return stream;
  }

  bool createDirectory() override {
    FRESULT r = f_mkdir(fatPath(_path).c_str());
    if (r == FR_OK || r == FR_EXIST) {
      _exists = _isDir = true;
      return true;
    }
    return false;
  }

private:
  Common::String _path;
  bool _isDir = false;
  bool _exists = false;
};

}  // namespace

AbstractFSNode *FatFsFilesystemFactory::makeRootFileNode() const {
  return new FatFsNode("/");
}

AbstractFSNode *FatFsFilesystemFactory::makeCurrentDirectoryFileNode() const {
  return new FatFsNode("/");
}

AbstractFSNode *FatFsFilesystemFactory::makeFileNodePath(const Common::String &path) const {
  return new FatFsNode(path);
}
