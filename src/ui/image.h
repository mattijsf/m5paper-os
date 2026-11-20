// Pictures for the UI: a cover or a photo from a file, decoded and scaled on whatever thread calls it (the worker, normally).
#pragma once

#include <memory>
#include <string>

#include "core/gfx.h"

namespace pos::ui {

// Decodes a JPEG or PNG file (virtual path) and scales it, keeping the aspect, to fit a boxW x boxH gray canvas, centred on
// white with a one pixel margin. JPEGs above about 1.5 megapixels are decoded at 1/2, 1/4 or 1/8 size first. nullptr if the
// file is missing, is not a JPEG or PNG, is bigger than 12 megapixels or cannot be decoded.
std::shared_ptr<Canvas> loadImageFit(const std::string& virtualPath, int boxW, int boxH);
// The same for a JPEG or PNG already in memory (a cover read over the network).
std::shared_ptr<Canvas> loadImageFitData(const std::string& data, int boxW, int boxH);

}  // namespace pos::ui
