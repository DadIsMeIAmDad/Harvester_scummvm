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

#include "harvester/inventory.h"

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/endian.h"
#include "graphics/blit.h"
#include "graphics/font.h"
#include "graphics/screen.h"
#include "harvester/detection.h"
#include "harvester/harvester.h"
#include "harvester/menu.h"
#include "harvester/player.h"
#include "harvester/resources.h"
#include "harvester/art.h"

namespace Harvester {

namespace {

static const int kInventoryX = 64;
static const int kInventoryY = 48;
static const int kInventoryItemStartX = 73;
static const int kInventoryItemStartY = 115;
static const int kInventoryItemMaxRight = 564;
static const int kInventoryItemSpacing = 5;
static const byte kTransparentPaletteIndex = 0;
static const char *const kHarvestBladeObjectName = "HARVEST_BLADE";

struct InventoryCombatLoadoutEntry {
	const char *objectName;
	int loadoutId;
};

struct InventorySecondaryActionEntry {
	const char *objectName;
	const char *actionTag;
	bool closeInventory;
};

static const InventoryCombatLoadoutEntry kInventoryCombatLoadoutMap[] = {
	{ "CLEAVER", 1 },
	{ "NAILGUN", 2 },
	{ "SHOTGUN", 3 },
	{ "9GUN", 4 },
	{ "38GUN", 5 },
	{ "TOMAHAWK", 6 },
	{ "KNIFE", 7 },
	{ "FLAIL", 8 },
	{ "HANDAXE", 9 },
	{ "WRENCH", 10 },
	{ "PITCHFORK", 11 },
	{ "SCYTHE", 12 },
	{ "SWORD", 13 },
	{ "CHAINSAW", 14 },
	{ "HARVEST_BLADE", 15 },
	{ "SHOVEL", 16 },
	{ "FIREAXE", 17 },
	{ "BAT", 18 },
	{ "RAZOR", 19 },
	{ "POOLSTICK", 20 }
};

// Recovered from native run_inventory_screen: right-clicked document/photo items dispatch
// hardcoded closeup action tags instead of using their object records directly.
static const InventorySecondaryActionEntry kInventorySecondaryActionMap[] = {
	{ "NOTE_PHOTOCOPY", "GO_BOYLCOPYCU", true },
	{ "NOTE", "GO_BOYLNOTECU", true },
	{ "PHOTO_OF_WHALEY_HERRILL_PHOTOCOPY", "GO_BROOMCPYCU", true },
	{ "PHOTO_OF_WHALEY_HERRILL", "GO_BROOMPICCU", true },
	{ "CASKET_PHOTOCOPY", "GO_CASKTCPYCU", true },
	{ "CASKET_PHOTO", "GO_CASKTPICCU", true },
	{ "LODGE_APPLICATION", "GO_LODGAPP1CU", true },
	{ "COMPLETED_LODGE_APPLICATION", "GO_LODGAPP2CU", true },
	{ "MEAT_PERMISSION0", "GO_PERMIT1CU", true },
	{ "MEAT_PERMISSION", "GO_PERMIT2CU", true },
	{ "CHECKBOOK", "GO_REGISTERCU", true },
	{ "CHECKBOOK_PHOTOCOPY", "GO_RGSTRCPYCU", true },
	{ "SAFEBOOK", "GO_SAFEBOOKCU", true },
	{ "PATROL_SCHED", "GO_SCHEDULECU", true },
	{ "TV_DEED", "GO_TVDEED1CU", true },
	{ "TV_DEED_PHOTOCOPY", "GO_TVDEED2CU", true },
	{ "INVITE", "GO_INVITECU", true },
	{ "CLUE", "GOTO_CLUE_CU", true },
	{ "AUTOGRAPH", "GO_AUTOGRPHCU", true }
};

static const char *const kInventoryObjectActionItems[] = {
	"SANDWICH",
	"SANDWICH2",
	"SYRINGE",
	"ST_ASPRIN",
	"ST_COUGHM",
	"ST_VITAMN"
};

static void blitBitmap(Graphics::Screen &screen, const IndexedBitmap &bitmap, int x, int y) {
	if (!bitmap.isValid())
		return;

	int destX = x;
	int destY = y;
	int srcX = 0;
	int srcY = 0;
	int width = (int)bitmap.width;
	int height = (int)bitmap.height;

	if (destX < 0) {
		srcX = -destX;
		width += destX;
		destX = 0;
	}
	if (destY < 0) {
		srcY = -destY;
		height += destY;
		destY = 0;
	}
	if (destX >= screen.w || destY >= screen.h || width <= 0 || height <= 0)
		return;

	width = MIN<int>(width, screen.w - destX);
	height = MIN<int>(height, screen.h - destY);
	if (width <= 0 || height <= 0)
		return;

	const byte *src = bitmap.pixels.data() + srcY * bitmap.width + srcX;
	byte *dst = (byte *)screen.getBasePtr(destX, destY);
	Graphics::keyBlit(dst, src, screen.pitch, bitmap.width, width, height,
		screen.format.bytesPerPixel, kTransparentPaletteIndex);
}

static Common::Rect getHotspotBounds(const ObjectRecord &object) {
	if (object.boundsX2 > object.currentX && object.boundsY2 > object.currentY)
		return Common::Rect(object.currentX, object.currentY, object.boundsX2 + 1, object.boundsY2 + 1);

	return Common::Rect();
}

static Common::String resolveSceneObjectSpritePath(const ObjectRecord &object) {
	const bool atInitialPlacement = object.currentX == object.initialX &&
		object.currentY == object.initialY &&
		object.currentOwnerOrRoom.equalsIgnoreCase(object.initialOwnerOrRoom);
	if (!object.altSpritePath.empty() &&
		(!atInitialPlacement || object.currentOwnerOrRoom.equalsIgnoreCase("INVENTORY")))
		return object.altSpritePath;

	return object.spritePath;
}

static bool loadBitmapResource(ResourceManager &resources, const Common::String &path, IndexedBitmap &bitmap) {
	Common::Array<byte> data;
	if (!resources.loadFile(path, data) || data.size() < 12)
		return false;

	// Reject PNG / non-BM payloads
	if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
		warning("Harvester: inventory expected BM but got PNG '%s'", path.c_str());
		return false;
	}

	bitmap = IndexedBitmap();
	bitmap.width = READ_LE_UINT32(data.data());
	bitmap.height = READ_LE_UINT32(data.data() + 4);

	// Sanity: original game is low-res; anything huge is corrupt / wrong format
	if (bitmap.width == 0 || bitmap.height == 0 ||
			bitmap.width > 2048 || bitmap.height > 2048)
		return false;

	const uint32 pixelCount = bitmap.width * bitmap.height;
	if (data.size() < 12 + pixelCount)
		return false;

	bitmap.pixels.resize(pixelCount);
	memcpy(bitmap.pixels.data(), data.data() + 12, pixelCount);
	return true;
}

static bool loadPngResource(ResourceManager &resources, const Common::String &path,
		Graphics::Surface *&outSurface) {
	outSurface = nullptr;
	Common::SeekableReadStream *stream = resources.openFile(path);
	if (!stream)
		return false;

	Image::PNGDecoder decoder;
	if (!decoder.loadStream(*stream)) {
		delete stream;
		return false;
	}
	delete stream;

	const Graphics::Surface *src = decoder.getSurface();
	if (!src)
		return false;

	outSurface = new Graphics::Surface();
	outSurface->copyFrom(*src);
	return true;
}

static bool resolveInventoryCombatLoadoutId(const Common::String &objectName, int &loadoutId) {
	loadoutId = 0;
	if (objectName.empty())
		return false;

	for (const InventoryCombatLoadoutEntry &entry : kInventoryCombatLoadoutMap) {
		if (objectName.equalsIgnoreCase(entry.objectName)) {
			loadoutId = entry.loadoutId;
			return true;
		}
	}

	return false;
}

static bool resolveInventorySecondaryActionEntry(const Common::String &objectName,
		InventorySecondaryAction &action) {
	action = InventorySecondaryAction();
	if (objectName.empty())
		return false;

	for (const InventorySecondaryActionEntry &entry : kInventorySecondaryActionMap) {
		if (!objectName.equalsIgnoreCase(entry.objectName))
			continue;

		action.actionTag = entry.actionTag;
		action.closeInventory = entry.closeInventory;
		return true;
	}

	return false;
}

static bool usesObjectActionForInventorySecondaryClick(const Common::String &objectName) {
	if (objectName.empty())
		return false;

	for (const char *itemName : kInventoryObjectActionItems) {
		if (objectName.equalsIgnoreCase(itemName))
			return true;
	}

	return false;
}

static void debugLogInventoryVisual(const InventoryVisual &visual, const Common::String &spritePath) {
	debugC(1, kDebugInventory,
		"Harvester: inventory visual object='%s' sprite='%s' alt='%s' chosen='%s' bounds=(%d,%d)-(%d,%d) action='%s' owner='%s' text='%s'",
		visual.object.objectName.c_str(), visual.object.spritePath.c_str(), visual.object.altSpritePath.c_str(),
		spritePath.c_str(), visual.bounds.left, visual.bounds.top, visual.bounds.right, visual.bounds.bottom,
		visual.object.actionTag.c_str(), visual.object.currentOwnerOrRoom.c_str(),
		visual.object.inventoryTextKey.c_str());
}

} // End of anonymous namespace

InventorySystem::InventorySystem(HarvesterEngine &engine) : _engine(engine) {
}

bool InventorySystem::loadBitmap(const Common::String &path, IndexedBitmap &bitmap) {
	bitmap = IndexedBitmap();
	if (path.empty())
		return false;

	if (_bitmapCache.contains(path)) {
		bitmap = _bitmapCache[path];
		return bitmap.isValid();
	}

	ResourceManager *resources = _engine.getResources();
	if (!resources || !loadBitmapResource(*resources, path, bitmap))
		return false;

	_bitmapCache[path] = bitmap;
	return true;
}
Worked for 4s

That block is the main menu inventory art inside renderMainMenuScreen, not a separate inventory UI. Two issues there:

    fillRect(..., 0) clears the whole frame (no room/menu backdrop).
    copyRectToSurface ignores PNG alpha.

Fix this block (main menu)

screen->fillRect(screen->getBounds(),
	screen->format.bytesPerPixel == 1
		? 0
		: screen->format.RGBToColor(0, 0, 0));

const Graphics::Surface *inventorySurface = art->getInventorySurface();
if (inventorySurface) {
	// Alpha path — same as logo / Entity::draw
	screen->blitFrom(*inventorySurface,
		Common::Rect(0, 0, inventorySurface->w, inventorySurface->h),
		Common::Point(kInventoryX, kInventoryY));
} else {
	blitTransparentBitmap(*screen, art->getInventoryBitmap(), kInventoryX, kInventoryY);
}

Do the logo the same way right after (if you haven’t already):

if (const Graphics::Surface *logo = art->getLogoSurface()) {
	screen->blitFrom(*logo,
		Common::Rect(0, 0, logo->w, logo->h),
		Common::Point(kLogoX, kLogoY));
} else {
	blitTransparentBitmap(*screen, art->getLogoBitmap(), kLogoX, kLogoY);
}

On the startup main menu, a black clear + inventory + logo is normal. You only need a captured backdrop when inventory is an overlay on the game room.
If you mean a real in-game inventory overlay

Then don’t clear to black. Capture first, then draw:

// On open:
if (screen->format.bytesPerPixel != 1) {
	_inventoryBackdropSurface = new Graphics::Surface();
	_inventoryBackdropSurface->copyFrom(*screen);
}

// Each redraw:
if (_inventoryBackdropSurface) {
	screen->copyRectToSurface(
		_inventoryBackdropSurface->getPixels(),
		_inventoryBackdropSurface->pitch,
		0, 0,
		_inventoryBackdropSurface->w,
		_inventoryBackdropSurface->h);
}

// Then panel with alpha:
if (inventorySurface) {
	screen->blitFrom(*inventorySurface,
		Common::Rect(0, 0, inventorySurface->w, inventorySurface->h),
		Common::Point(kInventoryX, kInventoryY));
}

Summary
Screen	Background	Inventory PNG
Startup main menu	black fill is fine	blitFrom (not copyRectToSurface)
In-game inventory overlay	captured room surface first	blitFrom on top

Right now you’re on the main-menu path: switch to blitFrom and transparency should match the Options logo.

The in game inventory screen (press "i" or click character) --- I thought I fixed it. yes it's path is called from art.cpp --->  	if (!loadPngAsSurface(resources, "4:/GRAPHIC/OTHER/INVENTRY.png", _inventorySurface)) {
        debugC(2, kDebugResources,
            "Harvester: INVENTRY.png not found, falling back to INVENTRY.BM");
        if (!loadBitmap(resources, "1:/GRAPHIC/OTHER/INVENTRY.BM", _inventoryBitmap))
            return false;
    }
Worked for 9s

In-game inventory is engines/harvester/inventory.cpp, not menu.cpp.

Art only loads INVENTRY.png; drawing is here:

void InventorySystem::drawOverlay(Graphics::Screen &screen) const {
	...
	blitBitmap(screen, art->getInventoryBitmap(), kInventoryX, kInventoryY);  // BM only!

That never uses _inventorySurface.
Fix drawOverlay

void InventorySystem::drawOverlay(Graphics::Screen &screen) const {
	const Art *art = _engine.getArt();
	if (!art)
		return;

	// Prefer PNG panel with alpha (same as logo / Entity::draw)
	if (const Graphics::Surface *panel = art->getInventorySurface()) {
		screen.blitFrom(*panel,
			Common::Rect(0, 0, panel->w, panel->h),
			Common::Point(kInventoryX, kInventoryY));
	} else {
		blitBitmap(screen, art->getInventoryBitmap(), kInventoryX, kInventoryY);
	}

	for (const InventoryVisual &item : _items) {
		if (!_selectedItemName.empty() && item.object.objectName.equalsIgnoreCase(_selectedItemName))
			continue;
		if (item.hasBitmap && item.bitmap.isValid())
			blitBitmap(screen, item.bitmap, item.object.currentX, item.object.currentY);
	}
}

Fix getPanelBounds (so exit/hit tests match PNG size)

Common::Rect InventorySystem::getPanelBounds() const {
	const Art *art = _engine.getArt();
	if (!art)
		return Common::Rect();

	if (const Graphics::Surface *panel = art->getInventorySurface()) {
		return Common::Rect(kInventoryX, kInventoryY,
			kInventoryX + panel->w, kInventoryY + panel->h);
	}

	const IndexedBitmap &bitmap = art->getInventoryBitmap();
	if (!bitmap.isValid())
		return Common::Rect();

	return Common::Rect(kInventoryX, kInventoryY,
		kInventoryX + (int)bitmap.width, kInventoryY + (int)bitmap.height);
}

Background

For in-game inventory, the background is the room scene, drawn before drawOverlay(). You do not need a separate capture in inventory.cpp if the room loop already redraws the room every frame while inventory is open (same pattern as Quick Tips over Options).

If you still get mouse trails with inventory open, the room redraw is being skipped while _inventory.isOpen(). Ensure the room path still does:

    Draw room
    inventory.drawOverlay(screen)
    Cursor
    makeAllDirty / update

every frame.

Summary
Piece	File	Change
Load PNG	art.cpp	Already done
Draw panel	inventory.cpp drawOverlay	Use getInventorySurface() + blitFrom
Hit bounds	inventory.cpp getPanelBounds	Use surface size when present
Backdrop	Room loop	Keep drawing room under the overlay
10 sources

i get crash before i hit the draw stage. no error.
Worked for 30s

A crash before drawOverlay almost always means it dies in InventorySystem::open() ? refresh(), while loading item sprites — not while drawing the panel.
Most likely cause

refresh() does:

loadBitmap(spritePath, visual.bitmap);

and loadBitmap always uses BM layout (width/height at offset 0, then width*height pixels).

If any inventory sprite path is a .png (or a bad/empty path that still returns data), those header bytes become a huge width/height ? giant resize / memcpy ? silent crash. No engine warning.

That matches “no error, never reaches draw.”
Step 1 — Confirm with logs

At the top of refresh() and inside the item loop:

warning("INV: refresh start");
...
warning("INV: loading sprite '%s' for '%s'", spritePath.c_str(), inventoryObject.objectName.c_str());
if (!spritePath.empty() && loadBitmap(spritePath, visual.bitmap)) {
	warning("INV: loaded ok %ux%u", visual.bitmap.width, visual.bitmap.height);

Last line printed = which path kills you.
Step 2 — Harden loadBitmapResource (stops the crash)

In inventory.cpp (same pattern as menu):

static bool loadBitmapResource(ResourceManager &resources, const Common::String &path, IndexedBitmap &bitmap) {
	Common::Array<byte> data;
	if (!resources.loadFile(path, data) || data.size() < 12)
		return false;

	// Reject PNG / non-BM payloads
	if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
		warning("Harvester: inventory expected BM but got PNG '%s'", path.c_str());
		return false;
	}

	bitmap = IndexedBitmap();
	bitmap.width = READ_LE_UINT32(data.data());
	bitmap.height = READ_LE_UINT32(data.data() + 4);

	// Sanity: original game is low-res; anything huge is corrupt / wrong format
	if (bitmap.width == 0 || bitmap.height == 0 ||
			bitmap.width > 2048 || bitmap.height > 2048)
		return false;

	const uint32 pixelCount = bitmap.width * bitmap.height;
	if (data.size() < 12 + pixelCount)
		return false;

	bitmap.pixels.resize(pixelCount);
	memcpy(bitmap.pixels.data(), data.data() + 12, pixelCount);
	return true;
}

Step 3 — If items are PNG, load them as surfaces (or skip for now)

If the log shows .png paths, either:

A. Keep using .BM / alt BM paths for inventory icons, or

B. Add a PNG path (later): store Graphics::Surface * on InventoryVisual and blitFrom in drawOverlay / drag — same as entities.

Quick safe path: if path ends with .png, try a BM sibling or skip the icon instead of parsing as BM:

if (path.hasSuffixIgnoreCase(".png")) {
	Common::String bmPath = path;
	// optional: rewrite to .BM under 1:/ if you still have those
	warning("Harvester: skipping PNG inventory icon for BM loader '%s'", path.c_str());
	return false;
}

Step 4 — Still fix panel bounds (doesn’t cause this crash, but needed)

if (const Graphics::Surface *panel = art->getInventorySurface()) {
	return Common::Rect(kInventoryX, kInventoryY,
		kInventoryX + panel->w, kInventoryY + panel->h);
}

Empty BM + width/height == 0 is usually safe; the sprite load is the dangerous part.
Checklist
When	What fails
open() / refresh()	Item sprite loaded as BM but file is PNG ? crash
drawOverlay	Only wrong graphics / no alpha — not a silent pre-draw crash

Add the warnings, open inventory once, and note the last INV: loading sprite '...' line. That path is the one to fix (BM vs PNG). If you paste that log line, we can wire the exact loader for it.

cool. Inventory background draws, the screen background draws behind that. Health images do not show as png, crashes when I open inventory screen while holding items. -----> last log ---->  WARNING: INV: loading sprite '4:/GRAPHIC/INVENTRY/BAT.png' for 'BAT'!
WARNING: Harvester: inventory expected BM but got PNG '4:/GRAPHIC/INVENTRY/BAT.png'!
WARNING: INV: loading sprite '4:/GRAPHIC/INVENTRY/NINEMM.png' for '9GUN'!
WARNING: Harvester: inventory expected BM but got PNG '4:/GRAPHIC/INVENTRY/NINEMM.png'!
WARNING: INV: loading sprite '1:/GRAPHIC/INVENTRY/FIREAXE.BM' for 'FIREAXE'!
WARNING: INV: loading sprite '1:/GRAPHIC/INVENTRY/WRENCH.BM' for 'WRENCH'!
WARNING: INV: loading sprite '1:/GRAPHIC/INVENTRY/PITCHFK.BM' for 'PITCHFORK'!
WARNING: INV: loading sprite '1:/GRAPHIC/INVENTRY/SHOVEL.BM' for 'SHOVEL'!
WARNING: INV: loading sprite '4:/graphic/other/head-a1.png' for 'INV_STAT1'!
WARNING: Harvester: inventory expected BM but got PNG '4:/graphic/other/head-a1.png'!
Worked for 9s

Logs show the real issue: many inventory icons (and health heads) are PNG, but refresh() still only has a BM loader. Those skip load, and a mix of PNG/BM items under load can still crash or leave bad state.

Add a PNG path on InventoryVisual, same idea as entities.
1. Extend InventoryVisual (inventory.h)

struct InventoryVisual {
	ObjectRecord object;
	IndexedBitmap bitmap;
	Graphics::Surface *surface = nullptr;  // NEW — owned PNG
	Common::Rect bounds;
	bool hasBitmap = false;
	bool hasSurface = false;               // NEW
};

You’ll need a free helper when clearing _items so surfaces don’t leak.
2. Free surfaces when refreshing (inventory.cpp)

static void freeInventoryVisual(InventoryVisual &visual) {
	if (visual.surface) {
		visual.surface->free();
		delete visual.surface;
		visual.surface = nullptr;
	}
	visual.hasSurface = false;
	visual.hasBitmap = false;
	visual.bitmap = IndexedBitmap();
}

bool InventorySystem::refresh() {
	for (InventoryVisual &item : _items)
		freeInventoryVisual(item);
	_items.clear();
	Script *script = _engine.getScript();
	ResourceManager *resources = _engine.getResources();
	if (!script || !resources)
		return false;

	_lastPlayerHitPoints = script->getPlayerCurrentHitPoints();
	_lastStoryDayIndex = script->getCurrentStoryDayIndex();
	_lastHasHarvestBlade = script->isObjectInInventory(kHarvestBladeObjectName);

	Common::Array<ObjectRecord> inventoryObjects;
	script->getVisibleInventoryObjects(inventoryObjects);
	int nextX = kInventoryItemStartX;
	int nextY = kInventoryItemStartY;
	int rowHeight = 0;

	for (const ObjectRecord &inventoryObject : inventoryObjects) {
		InventoryVisual visual;
		visual.object = inventoryObject;

		if (isExitObject(inventoryObject)) {
			visual.bounds = getHotspotBounds(inventoryObject);
			_items.push_back(Common::move(visual));
			continue;
		}

		const Common::String spritePath = resolveSceneObjectSpritePath(inventoryObject);
		if (!spritePath.empty()) {
			if (spritePath.hasSuffixIgnoreCase(".png")) {
				if (loadPngResource(*resources, spritePath, visual.surface)) {
					visual.hasSurface = true;
					const int w = visual.surface->w;
					const int h = visual.surface->h;

					if (isStatusObject(inventoryObject)) {
						visual.bounds = Common::Rect(
							visual.object.currentX, visual.object.currentY,
							visual.object.currentX + w, visual.object.currentY + h);
						_items.push_back(Common::move(visual));
						continue;
					}

					if (nextX + w > kInventoryItemMaxRight) {
						nextX = kInventoryItemStartX;
						nextY += rowHeight + kInventoryItemSpacing;
						rowHeight = 0;
					}

					visual.object.currentX = nextX;
					visual.object.currentY = nextY;
					visual.bounds = Common::Rect(nextX, nextY, nextX + w, nextY + h);
					nextX += w + kInventoryItemSpacing;
					rowHeight = MAX(rowHeight, h);
				}
			} else if (loadBitmap(spritePath, visual.bitmap)) {
				visual.hasBitmap = true;
			if (isStatusObject(inventoryObject)) {
				visual.bounds = Common::Rect(visual.object.currentX, visual.object.currentY,
					visual.object.currentX + visual.bitmap.width, visual.object.currentY + visual.bitmap.height);
				_items.push_back(Common::move(visual));
				continue;
			}

			if (nextX + (int)visual.bitmap.width > kInventoryItemMaxRight) {
				nextX = kInventoryItemStartX;
				nextY += rowHeight + kInventoryItemSpacing;
				rowHeight = 0;
			}

			visual.object.currentX = nextX;
			visual.object.currentY = nextY;
			visual.bounds = Common::Rect(nextX, nextY, nextX + visual.bitmap.width, nextY + visual.bitmap.height);
			nextX += visual.bitmap.width + kInventoryItemSpacing;
			rowHeight = MAX<int>(rowHeight, visual.bitmap.height);
		} else {
			visual.bounds = getHotspotBounds(inventoryObject);
		}

		if (isStatusObject(inventoryObject)) {
			debugLogInventoryVisual(visual, spritePath);
			_items.push_back(Common::move(visual));
			continue;
		}

		debugLogInventoryVisual(visual, spritePath);
		_items.push_back(Common::move(visual));
	}

	if (_selectedItemName.empty())
		return true;

	for (const InventoryVisual &item : _items) {
		if (item.object.objectName.equalsIgnoreCase(_selectedItemName))
			return true;
	}

	_selectedItemName.clear();
	_promptText.clear();
	return true;
}

bool InventorySystem::open() {
	const bool wasOpen = _open;
	_open = true;
	_promptText.clear();
	return refresh() || !wasOpen;
}

bool InventorySystem::close() {
	if (!_open)
		return false;

	_open = false;
	return true;
}

bool InventorySystem::clearSelection() {
	if (_selectedItemName.empty() && _promptText.empty())
		return false;

	_selectedItemName.clear();
	_promptText.clear();
	return true;
}

bool InventorySystem::refreshIfRuntimeStateChanged() {
	Script *script = _engine.getScript();
	if (!script)
		return false;

	const int currentHitPoints = script->getPlayerCurrentHitPoints();
	const int currentStoryDayIndex = script->getCurrentStoryDayIndex();
	const bool hasHarvestBlade = script->isObjectInInventory(kHarvestBladeObjectName);
	if (currentHitPoints == _lastPlayerHitPoints &&
			currentStoryDayIndex == _lastStoryDayIndex &&
			hasHarvestBlade == _lastHasHarvestBlade)
		return false;

	return refresh();
}

bool InventorySystem::isOpen() const {
	return _open;
}

bool InventorySystem::hasSelection() const {
	return !_selectedItemName.empty();
}

const Common::String &InventorySystem::getSelectedItemName() const {
	return _selectedItemName;
}

Common::String InventorySystem::resolveSelectedLabel() const {
	Script *script = _engine.getScript();
	if (!script || _selectedItemName.empty())
		return Common::String();

	for (const InventoryVisual &item : _items) {
		if (item.object.objectName.equalsIgnoreCase(_selectedItemName))
			return script->resolveObjectLabel(item.object);
	}

	Common::Array<ObjectRecord> inventoryObjects;
	script->getVisibleInventoryObjects(inventoryObjects);
	for (const ObjectRecord &item : inventoryObjects) {
		if (item.objectName.equalsIgnoreCase(_selectedItemName))
			return script->resolveObjectLabel(item);
	}

	return normalizeHarvesterResourcePath(_selectedItemName);
}

Common::String InventorySystem::buildSelectedPrompt(const Common::String &targetLabel,
		const MenuTextConfig &menuTextConfig) const {
	return buildUseItemPrompt(menuTextConfig, resolveSelectedLabel(), targetLabel);
}

void InventorySystem::selectItem(const Common::String &objectName) {
	_selectedItemName = objectName;
}

bool InventorySystem::toggleCombatLoadout(const ObjectRecord &object, int currentLoadout,
		bool &changed) {
	changed = false;

	int loadoutId = 0;
	if (!resolveInventoryCombatLoadoutId(object.objectName, loadoutId))
		return false;

	Script *script = _engine.getScript();
	if (!script)
		return false;

	// Native run_inventory_screen compares the clicked weapon against the live combat avatar loadout.
	const int savedLoadout = script->getPlayerCombatLoadout();
	const int activeLoadout = currentLoadout >= 0 ? currentLoadout : savedLoadout;
	const int nextLoadout = activeLoadout == loadoutId ? 0 : loadoutId;
	const bool scriptChanged = script->setPlayerCombatLoadout(nextLoadout);
	const int resultingLoadout = script->getPlayerCombatLoadout();
	changed = activeLoadout != nextLoadout;
	debugC(1, kDebugInventory,
		"Harvester: inventory combat toggle object='%s' active_loadout=%d('%s') saved_loadout=%d('%s') requested_loadout=%d('%s') changed=%d script_changed=%d resulting_loadout=%d('%s') damage=%d damage_type='%s'",
		object.objectName.c_str(),
		activeLoadout, Player::describeCombatLoadout(activeLoadout),
		savedLoadout, Player::describeCombatLoadout(savedLoadout),
		nextLoadout, Player::describeCombatLoadout(nextLoadout),
		changed, scriptChanged,
		resultingLoadout, Player::describeCombatLoadout(resultingLoadout),
		Player::resolveCombatLoadoutDamageAmount(resultingLoadout),
		Player::describeCombatDamageType(Player::resolveCombatLoadoutDamageType(resultingLoadout)));
	return true;
}

bool InventorySystem::resolveSecondaryAction(const ObjectRecord &object,
		InventorySecondaryAction &action) const {
	if (resolveInventorySecondaryActionEntry(object.objectName, action)) {
		debugC(1, kDebugInventory,
			"Harvester: inventory secondary action object='%s' resolved hardcoded action='%s' closeInventory=%d",
			object.objectName.c_str(), action.actionTag.c_str(), action.closeInventory);
		return true;
	}

	action = InventorySecondaryAction();
	if (object.actionTag.empty() || !usesObjectActionForInventorySecondaryClick(object.objectName)) {
		debugC(1, kDebugInventory,
			"Harvester: inventory secondary action object='%s' has no supported action (objectAction='%s' supported=%d)",
			object.objectName.c_str(), object.actionTag.c_str(),
			usesObjectActionForInventorySecondaryClick(object.objectName));
		return false;
	}

	action.actionTag = object.actionTag;
	debugC(1, kDebugInventory,
		"Harvester: inventory secondary action object='%s' falling back to object action='%s'",
		object.objectName.c_str(), action.actionTag.c_str());
	return true;
}

void InventorySystem::setPromptText(const Common::String &promptText) {
	_promptText = promptText;
}

const Common::String &InventorySystem::getPromptText() const {
	return _promptText;
}

Common::String InventorySystem::resolveWeekdayLabel(const MenuTextConfig &menuTextConfig) const {
	Script *script = _engine.getScript();
	if (!script || script->isObjectInInventory(kHarvestBladeObjectName))
		return Common::String();

	const int storyDayIndex = script->getCurrentStoryDayIndex();
	if (storyDayIndex < 1 || storyDayIndex > (int)menuTextConfig.weekdayLabels.size())
		return Common::String();

	return menuTextConfig.weekdayLabels[storyDayIndex - 1];
}

const InventoryVisual *InventorySystem::findItemAtPoint(const Common::Point &point) const {
	for (int i = (int)_items.size() - 1; i >= 0; --i) {
		if (_items[i].bounds.contains(point))
			return &_items[i];
	}

	return nullptr;
}

Common::Rect InventorySystem::getPanelBounds() const {
	const Art *art = _engine.getArt();
	if (!art)
		return Common::Rect();

	if (const Graphics::Surface *panel = art->getInventorySurface()) {
		return Common::Rect(kInventoryX, kInventoryY,
			kInventoryX + panel->w, kInventoryY + panel->h);
	}

	const IndexedBitmap &bitmap = art->getInventoryBitmap();
	if (!bitmap.isValid())
		return Common::Rect();

	return Common::Rect(kInventoryX, kInventoryY,
		kInventoryX + (int)bitmap.width, kInventoryY + (int)bitmap.height);
}

void InventorySystem::drawSelectedDragItem(Graphics::Screen &screen, const Common::Point &point) const {
	if (_selectedItemName.empty())
		return;

	for (const InventoryVisual &item : _items) {
		if (!item.object.objectName.equalsIgnoreCase(_selectedItemName))
			continue;

		if (item.hasSurface && item.surface) {
			const int drawX = point.x - item.surface->w / 2;
			const int drawY = point.y - item.surface->h / 2;
			screen.blitFrom(*item.surface,
				Common::Rect(0, 0, item.surface->w, item.surface->h),
				Common::Point(drawX, drawY));
			return;
		}

		if (item.hasBitmap && item.bitmap.isValid()) {
			const int drawX = point.x - (int)item.bitmap.width / 2;
			const int drawY = point.y - (int)item.bitmap.height / 2;
			blitBitmap(screen, item.bitmap, drawX, drawY);
			return;
		}

		return; // selected item has no drawable graphic
	}
}

void InventorySystem::drawOverlay(Graphics::Screen &screen) const {
	const Art *art = _engine.getArt();
	if (!art)
		return;

	if (const Graphics::Surface *panel = art->getInventorySurface()) {
		screen.blitFrom(*panel,
			Common::Rect(0, 0, panel->w, panel->h),
			Common::Point(kInventoryX, kInventoryY));
	} else {
		blitBitmap(screen, art->getInventoryBitmap(), kInventoryX, kInventoryY);
	}

	for (const InventoryVisual &item : _items) {
		if (!_selectedItemName.empty() &&
				item.object.objectName.equalsIgnoreCase(_selectedItemName))
			continue;

		if (item.hasSurface && item.surface) {
			screen.blitFrom(*item.surface,
				Common::Rect(0, 0, item.surface->w, item.surface->h),
				Common::Point(item.object.currentX, item.object.currentY));
		} else if (item.hasBitmap && item.bitmap.isValid()) {
			blitBitmap(screen, item.bitmap, item.object.currentX, item.object.currentY);
		}
	}
}

bool InventorySystem::isExitObject(const ObjectRecord &object) {
	return object.objectName.equalsIgnoreCase("INV_EXIT");
}

bool InventorySystem::isStatusObject(const ObjectRecord &object) {
	return object.objectName.hasPrefixIgnoreCase("INV_STAT");
}

} // End of namespace Harvester
