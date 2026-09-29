// ScummVM-Dateisystem über FatFs (USB-Stick, Laufwerk „0:“).
// Pfade sind absolut mit „/“ als Trenner; „/DISK01.LEC“ ist „0:/DISK01.LEC“.
#pragma once

#include "backends/fs/fs-factory.h"

class FatFsFilesystemFactory : public FilesystemFactory {
public:
  AbstractFSNode *makeRootFileNode() const override;
  AbstractFSNode *makeCurrentDirectoryFileNode() const override;
  AbstractFSNode *makeFileNodePath(const Common::String &path) const override;
};
