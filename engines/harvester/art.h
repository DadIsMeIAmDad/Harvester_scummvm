#ifndef HARVESTER_ART_H
#define HARVESTER_ART_H

#include "common/array.h"
#include "graphics/surface.h"

namespace Graphics {
class Screen;
}

namespace Harvester {

class ResourceManager;

struct IndexedBitmap {
	uint32 width = 0;
	uint32 height = 0;
	Common::Array<byte> pixels;

	bool isValid() const {
		return width != 0 && height != 0 &&
			pixels.size() >= width * height;
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

void drawRoomInspectText(Graphics::Screen &screen, const Art &art, const Graphics::Font &font,
		const ResolvedText &inspectText, bool useNativeFont) {
	const TextboxBitmap *textbox = resolveInspectTextboxBitmap(art, inspectText);
	if (!textbox || !textbox->isValid())
		return;

	art.blitTextbox(screen, *textbox, kIdentTextboxX, kIdentTextboxY);

	int textboxWidth = 0;

	if (textbox->pngSurface) {
		textboxWidth = textbox->pngSurface->w;
	} else if (textbox->indexed.isValid()) {
		textboxWidth = (int)textbox->indexed.width;
	}

	if (textboxWidth <= 0)
		return;

	if (useNativeFont) {
		drawWrappedText(screen, font, inspectText.value,
			kIdentTextboxX + kIdentTextboxTextInsetX,
			kIdentTextboxY + kIdentTextboxTextInsetY,
			MAX<int>(0, textboxWidth - 2),
			0,
			kNativeIdentTextLineSpacing,
			true);
		return;
	}

	drawWrappedShadowedText(screen, font, inspectText.value,
		kIdentTextboxX + kIdentTextboxTextInsetX,
		kIdentTextboxY + kIdentTextboxTextInsetY,
		MAX<int>(0, textboxWidth - (kIdentTextboxTextInsetX + 2)),
		kIdentTextColor);
}

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