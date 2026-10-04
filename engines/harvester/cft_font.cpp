/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "harvester/cft_font.h"

#include "common/algorithm.h"
#include "common/endian.h"
#include "graphics/surface.h"

namespace Harvester {

namespace {

static const uint32 kCftFontHeightOffset = 0x40;
static const uint32 kCftStartTableOffset = 0x42;
static const uint32 kCftWidthTableOffset = 0x242;
static const uint32 kCftSpaceWidthOffset = 0x442;
static const uint32 kCftGlyphCount = 256;

} // End of anonymous namespace

void wrapCftTextByCharacterCount(const Graphics::Font &font, const Common::String &text,
		int width, Common::Array<Common::String> &lines) {
	lines.clear();
	if (text.empty())
		return;

	Common::String wrappedText;
	for (uint i = 0; i < text.size(); ++i) {
		if (text[i] != '\r')
			wrappedText += text[i];
	}

	const int wrapCharsPerLine = width / MAX<int>(1, font.getCharWidth(' ') - 1);
	if (wrapCharsPerLine <= 0) {
		lines.push_back(Common::move(wrappedText));
		return;
	}

	uint lineStart = 0;
	while (lineStart < wrappedText.size()) {
		uint lineEnd = lineStart;
		while (lineEnd < wrappedText.size() && wrappedText[lineEnd] != '\n')
			++lineEnd;

		if (lineEnd - lineStart > (uint)wrapCharsPerLine) {
			uint breakPos = MIN<uint>(lineStart + (uint)wrapCharsPerLine, lineEnd - 1);
			while (breakPos > lineStart && wrappedText[breakPos] != ' ')
				--breakPos;

			if (breakPos > lineStart && wrappedText[breakPos] == ' ') {
				wrappedText.setChar('\n', breakPos);
				while (breakPos + 1 < wrappedText.size() && wrappedText[breakPos + 1] == ' ')
					wrappedText.deleteChar(breakPos + 1);
				lineStart = breakPos + 1;
				continue;
			}
		}

		lineStart = lineEnd + 1;
	}

	Common::String line;
	for (uint i = 0; i < wrappedText.size(); ++i) {
		if (wrappedText[i] == '\n') {
			lines.push_back(Common::move(line));
			line.clear();
			continue;
		}

		line += wrappedText[i];
	}

	if (!line.empty() || lines.empty())
		lines.push_back(Common::move(line));
}

HarvesterCftFont::HarvesterCftFont(const CftFontResource &resource) : _resource(resource) {
	if (_resource.header.size() < kCftWidthTableOffset + kCftGlyphCount * 2 || _resource.atlasWidth == 0 || _resource.atlasHeight == 0)
		return;

	const byte *header = _resource.header.data();
	_fontHeight = READ_LE_UINT16(header + kCftFontHeightOffset);
	_drawHeight = MAX<int>(0, (int)_resource.atlasHeight - 1);
	if (_fontHeight <= 0)
		_fontHeight = _drawHeight;
	_spaceWidth = READ_LE_UINT16(header + kCftSpaceWidthOffset);

	for (uint i = 0; i < kCftGlyphCount; ++i) {
		GlyphSlice &glyph = _glyphs[i];
		glyph.x = READ_LE_UINT16(header + kCftStartTableOffset + i * 2);
		glyph.width = READ_LE_UINT16(header + kCftWidthTableOffset + i * 2);
		if (glyph.width <= 0 || glyph.x >= (int)_resource.atlasWidth)
			continue;

		glyph.width = MIN<int>(glyph.width, (int)_resource.atlasWidth - glyph.x);
		glyph.valid = glyph.width > 0;
		if (glyph.valid)
			_maxCharWidth = MAX(_maxCharWidth, glyph.width);
	}

	if (_spaceWidth <= 0)
		_spaceWidth = MAX<int>(1, _maxCharWidth);
}

int HarvesterCftFont::getCharWidth(uint32 chr) const {
	if (chr == ' ' || chr == '_')
		return _spaceWidth;

	const GlyphSlice *glyph = findGlyph(chr);
	return glyph ? glyph->width : _spaceWidth;
}

void HarvesterCftFont::drawChar(Graphics::Surface *dst, uint32 chr, int x, int y, uint32 color) const {
	if (!dst || chr == ' ' || chr == '_')
		return;

	const GlyphSlice *glyph = findGlyph(chr);
	if (!glyph)
		return;

static const byte testPalette[256][3] = {
        {   0,  20,  91 }, // 0
        {   0,   0,   0 }, // 1
        {  18,  18,  18 }, // 2
        {  36,  36,  36 }, // 3
        {  57,  57,  49 }, // 4
        {  74,  74,  66 }, // 5
        {  91,  91,  91 }, // 6
        { 109, 109, 109 }, // 7
        { 132, 115, 107 }, // 8
        { 145, 145, 145 }, // 9
        { 163, 163, 163 }, // 10
        { 181, 173, 181 }, // 11
        { 200, 200, 200 }, // 12
        { 214, 198, 206 }, // 13
        { 236, 236, 236 }, // 14
        { 255, 255, 255 }, // 15
        { 116,  67,  41 }, // 16
        { 138,  77,  45 }, // 17
        { 161,  88,  49 }, // 18
        { 183,  98,  53 }, // 19
        { 206, 109,  57 }, // 20
        { 222, 127,  77 }, // 21
        { 238, 145,  98 }, // 22
        { 255, 164, 119 }, // 23
        {  23,  32,  22 }, // 24
        {  44,  63,  33 }, // 25
        {  66,  95,  45 }, // 26
        {  87, 127,  56 }, // 27
        { 109, 159,  68 }, // 28
        { 139, 191,  96 }, // 29
        { 169, 223, 125 }, // 30
        { 200, 255, 154 }, // 31
        {  49,  41,  33 }, // 32
        {  55,  44,  36 }, // 33
        {  61,  48,  39 }, // 34
        {  67,  52,  42 }, // 35
        {  73,  55,  45 }, // 36
        {  79,  59,  48 }, // 37
        {  85,  63,  51 }, // 38
        {  91,  67,  54 }, // 39
        {  97,  70,  57 }, // 40
        { 103,  74,  60 }, // 41
        { 109,  78,  63 }, // 42
        { 115,  82,  66 }, // 43
        { 121,  88,  70 }, // 44
        { 128,  95,  75 }, // 45
        { 134, 101,  80 }, // 46
        { 141, 108,  85 }, // 47
        { 148, 107,  90 }, // 48
        { 157, 115,  98 }, // 49
        { 166, 123, 106 }, // 50
        { 175, 131, 114 }, // 51
        { 185, 140, 123 }, // 52
        { 194, 148, 131 }, // 53
        { 203, 156, 139 }, // 54
        { 212, 164, 147 }, // 55
        { 222, 173, 156 }, // 56
        { 231, 173, 156 }, // 57
        { 235, 184, 168 }, // 58
        { 239, 195, 181 }, // 59
        { 243, 206, 193 }, // 60
        { 247, 217, 206 }, // 61
        { 251, 228, 218 }, // 62
        { 255, 239, 231 }, // 63
        {  66,  16,   8 }, // 64
        {  86,  30,  21 }, // 65
        { 106,  44,  34 }, // 66
        { 126,  58,  47 }, // 67
        { 146,  72,  60 }, // 68
        { 166,  86,  73 }, // 69
        { 186, 100,  86 }, // 70
        { 206, 115,  99 }, // 71
        { 115,  33,  24 }, // 72
        { 140,  29,  16 }, // 73
        { 165,  25,   8 }, // 74
        { 190,  22,   0 }, // 75
        { 194,  45,  24 }, // 76
        { 198,  68,  49 }, // 77
        { 202,  91,  74 }, // 78
        { 206, 115,  99 }, // 79
        {  24,  66, 115 }, // 80
        {  29,  71, 120 }, // 81
        {  35,  76, 126 }, // 82
        {  41,  82, 132 }, // 83
        {  63, 104, 148 }, // 84
        {  85, 126, 164 }, // 85
        { 107, 148, 181 }, // 86
        { 132, 181, 214 }, // 87
        {  41,  49,  57 }, // 88
        {  50,  61,  72 }, // 89
        {  60,  73,  87 }, // 90
        {  74,  90, 107 }, // 91
        { 111, 123, 140 }, // 92
        { 115, 123, 132 }, // 93
        { 148, 156, 173 }, // 94
        { 181, 181, 206 }, // 95
        {  74,  57,  33 }, // 96
        {  98,  73,  49 }, // 97
        { 123,  90,  66 }, // 98
        { 132, 106,  74 }, // 99
        { 142, 123,  82 }, // 100
        { 152, 134,  95 }, // 101
        { 162, 145, 109 }, // 102
        { 173, 156, 123 }, // 103
        { 184, 164, 128 }, // 104
        { 195, 172, 134 }, // 105
        { 206, 181, 140 }, // 106
        { 211, 194, 153 }, // 107
        { 216, 208, 167 }, // 108
        { 222, 222, 181 }, // 109
        { 239, 181, 173 }, // 110
        { 198, 140, 140 }, // 111
        { 107,  90,  49 }, // 112
        { 123, 115,  57 }, // 113
        { 140, 123,  66 }, // 114
        { 148, 148,  90 }, // 115
        { 173, 165,  99 }, // 116
        { 181, 173, 123 }, // 117
        {  82,  74,  24 }, // 118
        { 109,  97,  31 }, // 119
        { 136, 121,  38 }, // 120
        { 164, 145,  46 }, // 121
        { 185, 163,  51 }, // 122
        { 206, 181,  57 }, // 123
        { 217, 198,  96 }, // 124
        { 228, 216, 136 }, // 125
        { 239, 233, 176 }, // 126
        { 251, 251, 216 }, // 127
        { 255, 255, 255 }, // 128
        {  33,  49, 115 }, // 129
        {  16,  24,  57 }, // 130
        {  16,  24,  66 }, // 131
        {  24,  33,  82 }, // 132
        {  57,  74, 189 }, // 133
        {  57,  74, 198 }, // 134
        {  82,  90, 189 }, // 135
        {  82,  90, 206 }, // 136
        {  57,  66, 165 }, // 137
        {  49,  57, 148 }, // 138
        { 107, 115, 255 }, // 139
        {  82,  90, 222 }, // 140
        {  74,  74, 123 }, // 141
        {  57,  57, 107 }, // 142
        {  74,  74, 148 }, // 143
        {  49,  49,  99 }, // 144
        {  57,  57, 123 }, // 145
        {  49,  49, 107 }, // 146
        {  90,  90, 198 }, // 147
        {  66,  66, 148 }, // 148
        {  90,  90, 214 }, // 149
        {  41,  41,  99 }, // 150
        {  33,  33,  82 }, // 151
        {  49,  49, 123 }, // 152
        {  41,  41, 107 }, // 153
        {  66,  66, 173 }, // 154
        {  82,  82, 222 }, // 155
        {  57,  57, 156 }, // 156
        {  33,  33,  90 }, // 157
        {  74,  74, 206 }, // 158
        {  49,  49, 140 }, // 159
        {  82,  82, 239 }, // 160
        {  41,  41, 123 }, // 161
        {   8,   8,  24 }, // 162
        {  57,  57, 173 }, // 163
        {  24,  24,  74 }, // 164
        {  16,  16,  49 }, // 165
        {  57,  57, 181 }, // 166
        {   8,   8,  41 }, // 167
        { 156, 148, 255 }, // 168
        { 123, 115, 222 }, // 169
        { 115, 107, 222 }, // 170
        { 123, 115, 247 }, // 171
        { 107,  99, 222 }, // 172
        { 115, 107, 247 }, // 173
        {  99,  90, 214 }, // 174
        {  90,  82, 198 }, // 175
        {  82,  74, 189 }, // 176
        {  99,  90, 239 }, // 177
        {  74,  66, 189 }, // 178
        { 181, 173, 255 }, // 179
        { 173, 165, 255 }, // 180
        { 165, 156, 255 }, // 181
        { 115, 107, 189 }, // 182
        { 132, 123, 222 }, // 183
        { 107,  99, 189 }, // 184
        {  90,  82, 165 }, // 185
        {  82,  74, 173 }, // 186
        { 140, 132, 198 }, // 187
        { 132, 123, 198 }, // 188
        { 115, 107, 173 }, // 189
        { 123, 115, 189 }, // 190
        { 107,  99, 165 }, // 191
        {  99,  90, 165 }, // 192
        {  82,  74, 148 }, // 193
        { 132, 115, 255 }, // 194
        {  99,  82, 222 }, // 195
        { 107,  90, 247 }, // 196
        {  90,  74, 206 }, // 197
        {  99,  82, 239 }, // 198
        { 206, 198, 255 }, // 199
        { 173, 165, 222 }, // 200
        { 156, 148, 206 }, // 201
        { 148, 140, 198 }, // 202
        { 156, 140, 255 }, // 203
        { 148, 132, 247 }, // 204
        { 140, 123, 247 }, // 205
        {  66,  57, 123 }, // 206
        {  99,  82, 206 }, // 207
        { 214, 206, 255 }, // 208
        { 181, 173, 222 }, // 209
        { 156, 148, 198 }, // 210
        { 140, 132, 181 }, // 211
        {  90,  82, 132 }, // 212
        {  74,  66, 115 }, // 213
        { 156, 140, 247 }, // 214
        { 140, 123, 222 }, // 215
        { 222, 214, 255 }, // 216
        { 115, 107, 148 }, // 217
        { 189, 173, 255 }, // 218
        {  99,  90, 140 }, // 219
        {  82,  74, 115 }, // 220
        { 156, 140, 222 }, // 221
        { 165, 148, 239 }, // 222
        { 148, 132, 222 }, // 223
        {  66,  57, 107 }, // 224
        { 148, 123, 255 }, // 225
        {  33,  24,  74 }, // 226
        { 198, 189, 231 }, // 227
        { 132, 123, 165 }, // 228
        {  99,  90, 132 }, // 229
        { 132, 107, 222 }, // 230
        { 198, 181, 255 }, // 231
        { 123, 115, 148 }, // 232
        { 107,  99, 132 }, // 233
        { 189, 173, 239 }, // 234
        { 165, 148, 214 }, // 235
        { 132, 115, 181 }, // 236
        { 214, 198, 255 }, // 237
        {  99,  90, 123 }, // 238
        { 214, 206, 231 }, // 239
        { 115, 107, 132 }, // 240
        { 189, 173, 222 }, // 241
        { 206, 189, 239 }, // 242
        { 173, 156, 206 }, // 243
        { 206, 181, 255 }, // 244
        { 132, 123, 148 }, // 245
        { 189, 173, 214 }, // 246
        { 173, 156, 198 }, // 247
        { 198, 181, 222 }, // 248
        { 165, 148, 189 }, // 249
        { 132, 115, 156 }, // 250
        { 247, 239, 255 }, // 251
        { 148, 132, 165 }, // 252
        { 239, 222, 255 }, // 253
        { 165, 148, 181 }, // 254
        {   0,   0,   0 } // 255
};

	// Check the range of pixel values in this glyph.
	byte minSrc = 255;
	byte maxSrc = 0;
	uint countNonZero = 0;

	for (int row = 0; row < _drawHeight; ++row) {
		const byte *srcRow =
			_resource.atlasPixels.data() +
			row * _resource.atlasWidth +
			glyph->x;

		for (int col = 0; col < glyph->width; ++col) {
			const byte v = srcRow[col];

			if (v != 0) {
				if (v < minSrc)
					minSrc = v;
				if (v > maxSrc)
					maxSrc = v;
				++countNonZero;
			}
		}
	}



	for (int row = 0; row < _drawHeight; ++row) {
		const int dstY = y + row;
		if (dstY < 0 || dstY >= dst->h)
			continue;

		const byte *srcRow =
			_resource.atlasPixels.data() +
			row * _resource.atlasWidth +
			glyph->x;

		for (int col = 0; col < glyph->width; ++col) {
			const int dstX = x + col;
			const byte srcColor = srcRow[col];
			


			if (dstX < 0 || dstX >= dst->w || srcColor == 0)
				continue;

			switch (dst->format.bytesPerPixel) {
			case 1:
				*((byte *)dst->getBasePtr(dstX, dstY)) = srcColor;
				break;

			case 2:
				*((uint16 *)dst->getBasePtr(dstX, dstY)) = srcColor;
				break;

			case 4: {
				const byte r = testPalette[srcColor][0];
				const byte g = testPalette[srcColor][1];
				const byte b = testPalette[srcColor][2];

				const uint32 pixelColor = dst->format.RGBToColor(r, g, b);

				*((uint32 *)dst->getBasePtr(dstX, dstY)) = pixelColor;
				break;
			}

			default:
				break;
			}
		}
	}
}

const HarvesterCftFont::GlyphSlice *HarvesterCftFont::findGlyph(uint32 chr) const {
	if (chr >= ARRAYSIZE(_glyphs))
		return nullptr;

	const GlyphSlice &glyph = _glyphs[chr];
	return glyph.valid ? &glyph : nullptr;
}

} // End of namespace Harvester
