#ifndef HARVESTER_ART_H
#define HARVESTER_ART_H

#include "common/array.h"
#include "graphics/surface.h"

namespace Graphics {
class Screen;
}

namespace Harvester {

class ResourceManager;

private:
	bool loadPalette(ResourceManager &resources,
			const Common::String &path,
			byte *dest) const;

	bool loadBitmap(ResourceManager &resources,
			const Common::String &path,
			IndexedBitmap &bitmap) const;

	bool loadPngBitmap(ResourceManager &resources,
			const Common::String &path,
			TextboxBitmap &bitmap) const;

	bool loadAnimation(ResourceManager &resources,
			const Common::String &path,
			Common::Array<AbmFrame> &frames) const;

	bool decodeAnimationFrame(const byte *source,
			uint32 sourceSize,
			bool compressed,
			Common::Array<byte> &dest) const;

	void blitTransparentBitmap(Graphics::Screen &screen,
			const IndexedBitmap &bitmap,
			int x, int y) const;

	void blitTransparentAnimationFrame(
			Graphics::Screen &screen,
			const Common::Array<AbmFrame> &frames,
			uint frameIndex,
			int x, int y) const;

	byte _waitPalette[256 * 3] = { 0 };

	Common::Array<AbmFrame> _waitFrames;
	Common::Array<TextboxBitmap> _textboxes;
	Common::Array<IndexedBitmap> _ammoIcons;

	IndexedBitmap _inventoryBitmap;
	IndexedBitmap _logoBitmap;
	IndexedBitmap _tipsBitmap;
};

} // namespace Harvester

#endif