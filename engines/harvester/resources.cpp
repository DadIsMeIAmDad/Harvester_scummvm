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

#include "harvester/resources.h"

#include "common/debug.h"
#include "common/stream.h"
#include "harvester/detection.h"
#include "harvester/xfile_archive.h"

namespace Harvester {

namespace {

struct ArchiveSpec {
	char archiveSetId;
	const char *indexPath;
	const char *dataPath;
	int priority;
};

static const ArchiveSpec kArchiveSpecs[] = {
	{ '1', "INDEX.001", "HARVEST.DAT", 30 },
	{ '2', "INDEX.002", "SOUND.DAT", 29 },
	{ '3', "INDEX.003", "HARVEST2.DAT", 28 },
	{ '4', nullptr, nullptr, 27 }
};

static bool hasArchiveSetPrefix(const Common::String &path) {
	return path.size() >= 3 &&
		path[0] >= '0' && path[0] <= '9' &&
		path[1] == ':' && path[2] == '/';
}

static const ArchiveSpec *findArchiveSpec(char archiveSetId) {
	for (const ArchiveSpec &spec : kArchiveSpecs) {
		if (spec.archiveSetId == archiveSetId)
			return &spec;
	}

	return nullptr;
}

static bool mountZipAsArchiveSet(
		ResourceManager &rm,
		const Common::String &zipPath,
		char archiveSetId,
		int priority) {

	Common::SeekableReadStream *stream =
		SearchMan.createReadStreamForMember(
			Common::Path(zipPath, '/'));

	if (!stream) {
		warning(
			"Harvester: cannot open ZIP for set %c: %s",
			archiveSetId,
			zipPath.c_str());

		return false;
	}

	Common::Archive *archive =
		Common::makeZipArchive(stream);

	if (!archive) {
		warning(
			"Harvester: failed to create ZIP archive for set %c: %s",
			archiveSetId,
			zipPath.c_str());

		return false;
	}

	const Common::String name =
		Common::String::format(
			"harvester-zip-%c",
			archiveSetId);

	rm.mountArchive(
		name,
		archive,
		priority,
		true);

	debugC(
		1,
		kDebugResources,
		"Harvester: mounted ZIP %s as set %c",
		zipPath.c_str(),
		archiveSetId);

	return true;
}

static Common::String stripLeadingSlashes(const Common::String &path) {
	Common::String normalized = path;
	while (!normalized.empty() && normalized[0] == '/')
		normalized.erase(0, 1);
	return normalized;
}

static Common::String buildDiscArchiveName(int discNumber, char archiveSetId) {
	return Common::String::format("harvester-disc-%d-xfile-%c", discNumber, archiveSetId);
}

static Common::String buildDiscDirectoryFilePath(int discNumber, const Common::String &path) {
	return Common::String::format("CD%d/%s", discNumber, stripLeadingSlashes(path).c_str());
}

static Common::String resolveDiscArchiveStoragePath(int discNumber, const Common::String &path) {
	const Common::String normalized = stripLeadingSlashes(path);
	if (normalized.empty())
		return Common::String();

	const Common::String discPath = buildDiscDirectoryFilePath(discNumber, normalized);
	if (SearchMan.hasFile(Common::Path(discPath, '/')))
		return discPath;

	if (discNumber == ResourceManager::kFirstDiscNumber && SearchMan.hasFile(Common::Path(normalized, '/')))
		return normalized;

	return Common::String();
}

static Common::String resolveDiscLooseResourcePath(int discNumber, const Common::String &path) {
	const Common::String normalized = stripLeadingSlashes(path);
	if (normalized.empty())
		return Common::String();

	const Common::String discPath = buildDiscDirectoryFilePath(discNumber, normalized);
	if (SearchMan.hasFile(Common::Path(discPath, '/')))
		return discPath;

	if (SearchMan.hasFile(Common::Path(normalized, '/')))
		return normalized;

	return Common::String();
}

static Common::String normalizeHarvesterLookupPath(const Common::String &path) {
	Common::String normalized(path);

	for (uint i = 0; i < normalized.size(); ++i) {
		if (normalized[i] == '\\')
			normalized.setChar('/', i);
	}

	while (normalized.hasPrefix("./"))
		normalized.erase(0, 2);

	if (hasArchiveSetPrefix(normalized)) {
		const char archiveSetId = normalized[0];
		Common::String memberPath = stripLeadingSlashes(normalized.substr(3));
		return memberPath.empty()
			? Common::String::format("%c:/", archiveSetId)
			: Common::String::format("%c:/%s", archiveSetId, memberPath.c_str());
	}

	if (normalized.size() >= 3 && normalized[1] == ':' && normalized[2] == '/')
		normalized.erase(0, 3);

	return stripLeadingSlashes(normalized);
}

} // End of anonymous namespace

Common::String normalizeHarvesterResourcePath(const Common::String &path) {
	Common::String normalized(path);

	for (uint i = 0; i < normalized.size(); ++i) {
		if (normalized[i] == '\\')
			normalized.setChar('/', i);
	}

	while (normalized.hasPrefix("./"))
		normalized.erase(0, 2);

	if (normalized.size() >= 3 && normalized[1] == ':' && normalized[2] == '/')
		normalized.erase(0, 3);

	while (!normalized.empty() && normalized[0] == '/')
		normalized.erase(0, 1);

	return normalized;
}

ResourceManager::ResourceManager() {
	reset();
}

ResourceManager::~ResourceManager() {
	_search.clear();
}

void ResourceManager::reset() {
	_search.clear();
	_currentDisc = 1;
	_search.add("harvester-loose-files", &SearchMan, 0, false);
}

Common::String ResourceManager::normalizeResourcePath(const Common::String &path) const {
	return normalizeHarvesterLookupPath(path);
}

bool ResourceManager::mountStartupArchives() {
	return setCurrentDisc(_currentDisc);
}

bool ResourceManager::setCurrentDisc(int discNumber) {
	if (discNumber <= 0)
		return false;
	if (!ensureDiscMounted(discNumber))
		return false;

	unmountOtherDiscArchives(discNumber);
	_currentDisc = discNumber;
	debugC(1, kDebugResources, "Harvester: switched active disc to %d", discNumber);
	return true;
}

bool ResourceManager::ensureDiscMounted(int discNumber) {
	if (discNumber <= 0)
		return false;

	bool mountedAny = false;

	// ----- Existing XFile mounts for sets 1/2/3 -----
	for (const ArchiveSpec &spec : kArchiveSpecs) {
		if (spec.archiveSetId == '4')
			continue; // Set 4 is the ZIP-backed HD drive

		// Keep your existing XFile mounting code here.
		//
		// IMPORTANT:
		// Do not add the old INDEX.004 / HARVEST4.DAT
		// mounting code here.
	}

	// ----- Set 4: mount HARVEST4.ZIP as virtual 4:/ -----

	static const char *const kRemasterZipCandidates[] = {
		"CD1/HARVEST4.ZIP",
		"HARVEST4.ZIP",
		"HD/HARVEST4.ZIP",
		"remaster/HARVEST4.ZIP"
	};

	// Don't mount the same ZIP more than once.
	if (!_search.hasArchive("harvester-zip-4")) {
		for (const char *candidate : kRemasterZipCandidates) {
			if (!SearchMan.hasFile(Common::Path(candidate, '/')))
				continue;

			if (mountZipAsArchiveSet(*this, candidate, '4', 27)) {
				mountedAny = true;
				break;
			}
		}
	} else {
		mountedAny = true;
	}

	// If sets 1-3 were mounted successfully, this remains true.
	// Set 4 is optional, so don't fail the entire disc mount if
	// HARVEST4.ZIP isn't present.
	return mountedAny;
}

void ResourceManager::unmountOtherDiscArchives(int keepDiscNumber) {
	for (int discNumber = ResourceManager::kFirstDiscNumber;
			discNumber <= ResourceManager::kLastDiscNumber; ++discNumber) {
		if (discNumber == keepDiscNumber)
			continue;

		for (const ArchiveSpec &spec : kArchiveSpecs) {
			const Common::String archiveName = buildDiscArchiveName(discNumber, spec.archiveSetId);
			if (_search.hasArchive(archiveName)) {
				debugC(1, kDebugResources,
					"Harvester: unmounted disc %d set %c",
					discNumber, spec.archiveSetId);
				_search.remove(archiveName);
			}
		}
	}
}

Common::Archive *ResourceManager::getMountedDiscArchive(
		int discNumber, char archiveSetId) const {

	// Set 4 is our HD/remaster ZIP.
	if (archiveSetId == '4')
		return _search.getArchive("harvester-zip-4");

	// Sets 1-3 use the original XFile archives.
	return _search.getArchive(
		buildDiscArchiveName(discNumber, archiveSetId));
}

Common::Archive *ResourceManager::findArchiveForMember(char archiveSetId, const Common::Path &memberPath) const {
	Common::Archive *archive = getMountedDiscArchive(_currentDisc, archiveSetId);
	if (archive && archive->hasFile(memberPath))
		return archive;

	return nullptr;
}

bool ResourceManager::hasInMountedArchives(const Common::Path &memberPath) const {
	for (const ArchiveSpec &spec : kArchiveSpecs) {
		if (findArchiveForMember(spec.archiveSetId,
		    Common::Path(memberPath, '/')) != nullptr)
	    return true;

    if (spec.archiveSetId == '4')
	    return false;

    return !resolveDiscArchiveStoragePath(
	    _currentDisc, memberPath).empty();

	return false;
}

Common::SeekableReadStream *ResourceManager::openFromMountedArchives(const Common::Path &memberPath) const {
	for (const ArchiveSpec &spec : kArchiveSpecs) {
		Common::Archive *archive = findArchiveForMember(spec.archiveSetId, memberPath);
		if (!archive)
			continue;

		Common::SeekableReadStream *stream = archive->createReadStreamForMember(memberPath);
		if (stream)
			return stream;
	}

	return nullptr;
}

bool ResourceManager::hasFile(const Common::String &path) const {
	const Common::String normalized = normalizeResourcePath(path);

	if (normalized.empty())
		return false;

	// Explicit archive-set path such as:
	// 1:/GRAPHIC/...
	// 2:/...
	// 3:/...
	// 4:/GRAPHIC/ROOMS/PCROOM.PNG
	if (hasArchiveSetPrefix(normalized)) {
		const char archiveSetId = normalized[0];

		const ArchiveSpec *spec =
			findArchiveSpec(archiveSetId);

		const Common::String memberPath =
			stripLeadingSlashes(normalized.substr(3));

		if (!spec || memberPath.empty())
			return false;

		Common::Archive *archive =
			findArchiveForMember(
				spec->archiveSetId,
				Common::Path(memberPath, '/'));

		if (archive)
			return true;

		// IMPORTANT:
		// 4:/ exists ONLY inside HARVEST4.ZIP.
		// Don't fall back to CD1/ or loose files for set 4.
		if (archiveSetId == '4')
			return false;

		// Original behavior for sets 1-3.
		return !resolveDiscArchiveStoragePath(
			_currentDisc,
			memberPath).empty();
	}

	// Normal path without an explicit archive number.
	const Common::Path memberPath(normalized, '/');

	if (hasInMountedArchives(memberPath))
		return true;

	return !resolveDiscLooseResourcePath(
		_currentDisc,
		normalized).empty();
}

Common::SeekableReadStream *ResourceManager::openFile(
		const Common::String &path) const {

	const Common::String normalized =
		normalizeResourcePath(path);

	if (normalized.empty())
		return nullptr;

	Common::SeekableReadStream *stream = nullptr;

	// Explicit archive-set path:
	//
	// 1:/...
	// 2:/...
	// 3:/...
	// 4:/...
	if (hasArchiveSetPrefix(normalized)) {
		const char archiveSetId = normalized[0];

		const ArchiveSpec *spec =
			findArchiveSpec(archiveSetId);

		const Common::String memberPath =
			stripLeadingSlashes(normalized.substr(3));

		if (spec && !memberPath.empty()) {
			Common::Archive *archive =
				findArchiveForMember(
					spec->archiveSetId,
					Common::Path(memberPath, '/'));

			if (archive) {
				stream = archive->createReadStreamForMember(
					Common::Path(memberPath, '/'));
			}

			// Sets 1-3 can fall back to their physical
			// CD/archive storage.
			//
			// Set 4 must NOT do this because 4:/ is
			// exclusively the HARVEST4.ZIP virtual drive.
			if (!stream && archiveSetId != '4') {
				const Common::String loosePath =
					resolveDiscArchiveStoragePath(
						_currentDisc,
						memberPath);

				if (!loosePath.empty()) {
					stream =
						SearchMan.createReadStreamForMember(
							Common::Path(loosePath, '/'));
				}
			}
		}
	} else {
		// Normal resource path without 1:/, 2:/, etc.

		const Common::Path memberPath(normalized, '/');

		// First check loose files.
		const Common::String loosePath =
			resolveDiscLooseResourcePath(
				_currentDisc,
				normalized);

		if (!loosePath.empty()) {
			stream =
				SearchMan.createReadStreamForMember(
					Common::Path(loosePath, '/'));
		}

		// Then check mounted archives.
		if (!stream)
			stream = openFromMountedArchives(memberPath);
	}

	debugC(
		3,
		kDebugResources,
		"Harvester: openFile(disc=%d, '%s' -> '%s') %s",
		_currentDisc,
		path.c_str(),
		normalized.c_str(),
		stream ? "hit" : "miss"
	);

	return stream;
}

bool ResourceManager::loadFile(const Common::String &path, Common::Array<byte> &data) const {
	Common::ScopedPtr<Common::SeekableReadStream> stream(openFile(path));
	if (!stream)
		return false;

	data.resize(stream->size());
	if (!data.empty())
		stream->read(data.data(), data.size());

	return true;
}

void ResourceManager::mountArchive(const Common::String &name, Common::Archive *archive, int priority, bool autoFree) {
	_search.add(name, archive, priority, autoFree);
}

} // End of namespace Harvester
