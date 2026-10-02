/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of the developers of ScummVM.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HARVESTER_ART_H
#define HARVESTER_ART_H

#include "common/array.h"

namespace Graphics {
class Screen;
struct Surface;
}

namespace Harvester {

class ResourceManager;

struct IndexedBitmap {
	uint32 width = 0;
	uint32 height = 0;
	Common::Array<byte> pixels;

	bool isValid() const {
		return width != 0 && height != 0 && pixels.size() >= width * height;
	}
};

struct TextboxBitmap {
	IndexedBitmap indexed;
	Graphics::Surface *pngSurface = nullptr;

	bool isPng() const {
		return pngSurface != nullptr;
	}

	bool isValid() const {
		if (pngSurface)
			return pngSurface->w > 0 && pngSurface->h > 0;

		return indexed.isValid();
	}

	void free() {
		if (pngSurface) {
			pngSurface->free();
			delete pngSurface;
			pngSurface = nullptr;
		}

		indexed.width = 0;
		indexed.height = 0;
		indexed.pixels.clear();
	}
};

struct AbmFrame : IndexedBitmap {
	int32 xOffset = 0;
	int32 yOffset = 0;
};

class Art {
public:
	bool load(ResourceManager &resources);
	bool loadQuickTipsResources(ResourceManager &resources, bool useTextboxPanel);
	void drawWaitFrame(Graphics::Screen &screen) const;

	const byte *getWaitPalette() const { return _waitPalette; }
	const Common::Array<AbmFrame> &getWaitFrames() const {
		return _waitFrames;
	}

	const IndexedBitmap &getInventoryBitmap() const {
		return _inventoryBitmap;
	}

	const IndexedBitmap &getLogoBitmap() const {
		return _logoBitmap;
	}

	const IndexedBitmap &getTipsBitmap() const {
		return _tipsBitmap;
	}

	const TextboxBitmap *getQuickTipsTextboxBitmap() const;

	const Common::Array<IndexedBitmap> &getAmmoIcons() const {
		return _ammoIcons;
	}

	const TextboxBitmap *getTextboxBitmap(uint index) const {
		return index < _textboxes.size() ? &_textboxes[index] : nullptr;
	}

private:
	bool loadPalette(ResourceManager &resources,
			const Common::String &path, byte *dest) const;

	bool loadBitmap(ResourceManager &resources,
			const Common::String &path, IndexedBitmap &bitmap) const;

	bool loadPngBitmap(ResourceManager &resources,
			const Common::String &path, TextboxBitmap &bitmap) const;

	bool loadAnimation(ResourceManager &resources,
			const Common::String &path,
			Common::Array<AbmFrame> &frames) const;

	bool decodeAnimationFrame(const byte *source,
			uint32 sourceSize,
			bool compressed,
			Common::Array<byte> &dest) const;

	void blitTextbox(Graphics::Screen &screen,
			const TextboxBitmap &bitmap,
			int x,
			int y) const;

	void blitTransparentBitmap(Graphics::Screen &screen,
			const IndexedBitmap &bitmap,
			int x,
			int y) const;

	void blitTransparentAnimationFrame(Graphics::Screen &screen,
			const Common::Array<AbmFrame> &frames,
			uint frameIndex,
			int x,
			int y) const;

	byte _waitPalette[256 * 3] = { 0 };

	Common::Array<AbmFrame> _waitFrames;

	Common::Array<TextboxBitmap> _textboxes;

	Common::Array<IndexedBitmap> _ammoIcons;

	IndexedBitmap _inventoryBitmap;
	IndexedBitmap _logoBitmap;
	IndexedBitmap _tipsBitmap;
};

} // End of namespace Harvester

#endif // HARVESTER_ART_H