/******************************************************************************

 * Spine Runtimes License Agreement

 * Last updated April 5, 2025. Replaces all prior versions.

 *

 * Copyright (c) 2013-2025, Esoteric Software LLC

 *

 * Spine38Loader: loads Spine 3.8 editor exports (.skel / .json) into the 4.3

 * runtime by parsing them with an embedded, namespaced copy of the official

 * 3.8 spine-cpp runtime (spine38::) and translating the resulting

 * spine38::SkeletonData into a native spine::SkeletonData.

 *****************************************************************************/

#ifdef SPINE_GODOT_EXTENSION

#include "Spine38Loader.h"

#include <spine/spine.h>

#include <spine38/spine.h>

#include <map>
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>

using namespace spine;

namespace {

/// Simple string conversion between the two runtime string types.
static spine::String to43(const spine38::String &value) {
	const char *buffer = value.buffer();
	return spine::String(buffer ? buffer : "");
}

static std::string toStd(const spine38::String &value) {
	const char *buffer = value.buffer();
	return std::string(buffer ? buffer : "");
}

static void fail(spine::String &error, const char *message, const char *detail = 0) {
	error = spine::String(message);
	if (detail) {
		error.append(" ");
		error.append(detail);
	}
}

// -----------------------------------------------------------------------------
// Attachment loader used while parsing 3.8 data. It never touches an atlas:
// the 4.3 atlas rebuilds all UVs after translation. It only records the region
// path each attachment was created with, which the translator needs to look up
// the matching 4.3 atlas region.
// -----------------------------------------------------------------------------
class PlaceholderAttachmentLoader38 : public spine38::AttachmentLoader {
public:
	PlaceholderAttachmentLoader38() {
	}

	virtual ~PlaceholderAttachmentLoader38() {
	}

	const spine38::String &getPath(spine38::Attachment *attachment) const {
		static spine38::String empty;
		std::map<const spine38::Attachment *, spine38::String>::const_iterator it = _paths.find(attachment);
		return it != _paths.end() ? it->second : empty;
	}

	spine38::RegionAttachment *newRegionAttachment(spine38::Skin &skin, const spine38::String &name, const spine38::String &path) override {
		SP_UNUSED(skin);
		spine38::RegionAttachment *attachment = new (__FILE__, __LINE__) spine38::RegionAttachment(name);
		_paths[attachment] = spine38::String(path);
		return attachment;
	}

	spine38::MeshAttachment *newMeshAttachment(spine38::Skin &skin, const spine38::String &name, const spine38::String &path) override {
		SP_UNUSED(skin);
		spine38::MeshAttachment *attachment = new (__FILE__, __LINE__) spine38::MeshAttachment(name);
		_paths[attachment] = spine38::String(path);
		return attachment;
	}

	spine38::BoundingBoxAttachment *newBoundingBoxAttachment(spine38::Skin &skin, const spine38::String &name) override {
		SP_UNUSED(skin);
		return new (__FILE__, __LINE__) spine38::BoundingBoxAttachment(name);
	}

	spine38::PathAttachment *newPathAttachment(spine38::Skin &skin, const spine38::String &name) override {
		SP_UNUSED(skin);
		return new (__FILE__, __LINE__) spine38::PathAttachment(name);
	}

	spine38::PointAttachment *newPointAttachment(spine38::Skin &skin, const spine38::String &name) override {
		SP_UNUSED(skin);
		return new (__FILE__, __LINE__) spine38::PointAttachment(name);
	}

	spine38::ClippingAttachment *newClippingAttachment(spine38::Skin &skin, const spine38::String &name) override {
		SP_UNUSED(skin);
		return new (__FILE__, __LINE__) spine38::ClippingAttachment(name);
	}

	void configureAttachment(spine38::Attachment *attachment) override {
		SP_UNUSED(attachment);
	}

private:
	std::map<const spine38::Attachment *, spine38::String> _paths;
};

// -----------------------------------------------------------------------------
// Version sniffing
// -----------------------------------------------------------------------------
static const float CURVE_STEPPED = 1;
static const float CURVE_BEZIER = 2;

static bool read_varint(const uint8_t *data, size_t length, size_t &pos, int64_t &out) {
	if (pos >= length) return false;
	int64_t value = 0;
	int shift = 0;
	while (true) {
		if (pos >= length || shift > 63) return false;
		uint8_t byte = data[pos++];
		value |= (int64_t)(byte & 0x7F) << shift;
		if (!(byte & 0x80)) break;
		shift += 7;
	}
	out = value;
	return true;
}

static bool read_string38(const uint8_t *data, size_t length, size_t &pos, char *out, size_t outSize) {
	int64_t byteCount = 0;
	if (!read_varint(data, length, pos, byteCount)) return false;
	if (byteCount == 0) { // null string
		out[0] = '\0';
		return true;
	}
	byteCount -= 1; // encoded as length + 1, 1 means empty string
	if (byteCount < 0 || pos + (size_t) byteCount > length) return false;
	if ((size_t) byteCount >= outSize) return false;
	memcpy(out, data + pos, (size_t) byteCount);
	out[byteCount] = '\0';
	pos += (size_t) byteCount;
	return true;
}

static bool is_printable(const char *text) {
	if (!text) return false;
	size_t length = strlen(text);
	if (length == 0) return false;
	for (const char *c = text; *c; c++) {
		if ((unsigned char) *c < 0x20 || (unsigned char) *c > 0x7E) return false;
	}
	return true;
}

static bool version_is_3x(const char *version) {
	return version && strncmp(version, "3.", 2) == 0;
}
} // namespace

// The 3.8 runtime requires the integration to provide its allocator factory.
// The instance must be allocated with raw malloc because SpineObject's
// operator new routes through SpineExtension::getInstance() and would recurse
// while the instance is still being created.
namespace spine38 {
SpineExtension *getDefaultExtension() {
	void *memory = ::malloc(sizeof(DefaultSpineExtension));
	return new (memory) DefaultSpineExtension();
}
}

namespace Spine38Loader {

bool sniffs_binary_38(const uint8_t *data, size_t length, char *versionOut, size_t versionOutSize) {
	if (versionOut && versionOutSize) versionOut[0] = '\0';
	if (!data || length < 8) return false;
	// 3.8 binaries start with a varint encoded hash string followed by the
	// version string. 4.x binaries start with an 8 byte integer hash, so the
	// hash length varint would either span several bytes or exceed the file.
	size_t pos = 0;
	char hash[160];
	char version[32];
	if (!read_string38(data, length, pos, hash, sizeof(hash))) return false;
	if (!is_printable(hash)) return false;
	if (!read_string38(data, length, pos, version, sizeof(version))) return false;
	if (!is_printable(version)) return false;
	if (strlen(version) > 16) return false;
	if (!version_is_3x(version)) return false;
	if (versionOut && versionOutSize) {
		strncpy(versionOut, version, versionOutSize - 1);
		versionOut[versionOutSize - 1] = '\0';
	}
	return true;
}

bool sniffs_json_38(const char *json, char *versionOut, size_t versionOutSize) {
	if (versionOut && versionOutSize) versionOut[0] = '\0';
	if (!json) return false;
	const char *skeleton = strstr(json, "\"skeleton\"");
	if (!skeleton) return false;
	const char *spineKey = strstr(skeleton, "\"spine\"");
	if (!spineKey) return false;
	const char *p = strchr(spineKey + 7, ':');
	if (!p) return false;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p != '"') return false;
	p++;
	char version[32];
	size_t i = 0;
	while (*p && *p != '"' && i < sizeof(version) - 1) {
		version[i++] = *p++;
	}
	version[i] = '\0';
	if (*p != '"') return false;
	if (!version_is_3x(version)) return false;
	if (versionOut && versionOutSize) {
		strncpy(versionOut, version, versionOutSize - 1);
		versionOut[versionOutSize - 1] = '\0';
	}
	return true;
}

// -----------------------------------------------------------------------------
// Translation
// -----------------------------------------------------------------------------
namespace {

class Translator38 {
public:
	Translator38(spine::Atlas &atlas, spine::String &error) : _atlas(atlas), _error(error), _loader43(atlas) {
	}

	~Translator38() {
		delete _loader38;
	}

	spine::SkeletonData *translate(spine38::SkeletonData &src) {
		spine::SkeletonData *dst = new (__FILE__, __LINE__) spine::SkeletonData();

		dst->setVersion(to43(src.getVersion()));
		dst->setHash(to43(src.getHash()));
		dst->setImagesPath(to43(src.getImagesPath()));
		dst->setX(src.getX());
		dst->setY(src.getY());
		dst->setWidth(src.getWidth());
		dst->setHeight(src.getHeight());
		dst->setFps(src.getFps() > 0 ? src.getFps() : 30);

		if (!translateBones(src, *dst) || !translateSlots(src, *dst) || !translateConstraints(src, *dst) ||
			!translateEvents(src, *dst)) {
			delete dst;
			return NULL;
		}

		if (!translateSkins(src, *dst)) {
			delete dst;
			return NULL;
		}

		if (!translateAnimations(src, *dst)) {
			delete dst;
			return NULL;
		}

		return dst;
	}

private:
	const spine38::String &attachmentPath(spine38::Attachment *attachment) {
		static spine38::String empty;
		if (!_loader38) return empty;
		return _loader38->getPath(attachment);
	}

	static void copyFloatArray(spine38::Vector<float> &src, spine::Array<float> &dst) {
		dst.setSize((int) src.size(), 0);
		for (size_t i = 0; i < src.size(); i++) dst[i] = src[i];
	}

	static void copyBoneArray(spine38::Vector<size_t> &src, spine::Array<int> &dst) {
		dst.setSize((int) src.size(), 0);
		for (size_t i = 0; i < src.size(); i++) dst[i] = (int) src[i];
	}

	static void copyMeshData(spine38::MeshAttachment *src, spine::MeshAttachment *dst) {
		dst->setHullLength(src->getHullLength());
		copyBoneArray(src->getBones(), dst->getBones());
		copyFloatArray(src->getVertices(), dst->getVertices());
		dst->setWorldVerticesLength(src->getWorldVerticesLength());
		copyFloatArray(src->getRegionUVs(), dst->getRegionUVs());
		spine::Array<unsigned short> &triangles = dst->getTriangles();
		spine38::Vector<unsigned short> &srcTriangles = src->getTriangles();
		triangles.setSize((int) srcTriangles.size(), 0);
		for (size_t i = 0; i < srcTriangles.size(); i++) triangles[i] = srcTriangles[i];
		if (src->getEdges().size() > 0) {
			spine::Array<unsigned short> &edges = dst->getEdges();
			spine38::Vector<unsigned short> &srcEdges = src->getEdges();
			edges.setSize((int) srcEdges.size(), 0);
			for (size_t i = 0; i < srcEdges.size(); i++) edges[i] = srcEdges[i];
			dst->setWidth(src->getWidth());
			dst->setHeight(src->getHeight());
		}
	}

	bool translateBones(spine38::SkeletonData &src, spine::SkeletonData &dst) {
		spine38::Vector<spine38::BoneData *> &bones = src.getBones();
		for (size_t i = 0; i < bones.size(); i++) {
			spine38::BoneData *srcBone = bones[i];
			if (!srcBone) return false;
			spine::BoneData *parent = srcBone->getParent() ? _bones[srcBone->getParent()->getIndex()] : NULL;
			spine::BoneData *dstBone = new (__FILE__, __LINE__) spine::BoneData((int) i, to43(srcBone->getName()), parent);
			spine::BonePose &setup = dstBone->getSetupPose();
			setup.setRotation(srcBone->getRotation());
			setup.setX(srcBone->getX());
			setup.setY(srcBone->getY());
			setup.setScaleX(srcBone->getScaleX());
			setup.setScaleY(srcBone->getScaleY());
			setup.setShearX(srcBone->getShearX());
			setup.setShearY(srcBone->getShearY());
			setup.setInherit((spine::Inherit) srcBone->getTransformMode());
			dstBone->setLength(srcBone->getLength());
			dst.getBones().add(dstBone);
			_bones.push_back(dstBone);
		}
		return true;
	}

	bool translateSlots(spine38::SkeletonData &src, spine::SkeletonData &dst) {
		spine38::Vector<spine38::SlotData *> &slots = src.getSlots();
		for (size_t i = 0; i < slots.size(); i++) {
			spine38::SlotData *srcSlot = slots[i];
			if (!srcSlot) return false;
			spine::SlotData *dstSlot = new (__FILE__, __LINE__) spine::SlotData((int) i, to43(srcSlot->getName()),
																				*_bones[srcSlot->getBoneData().getIndex()]);
			spine::SlotPose &setup = dstSlot->getSetupPose();
			spine38::Color &color = srcSlot->getColor();
			setup.getColor().set(color.r, color.g, color.b, color.a);
			if (srcSlot->hasDarkColor()) {
				spine38::Color &dark = srcSlot->getDarkColor();
				setup.getDarkColor().set(dark.r, dark.g, dark.b, dark.a);
				setup.setHasDarkColor(true);
			}
			dstSlot->setAttachmentName(to43(srcSlot->getAttachmentName()));
			dstSlot->setBlendMode((spine::BlendMode) srcSlot->getBlendMode());
			dst.getSlots().add(dstSlot);
			_slots.push_back(dstSlot);
		}
		return true;
	}

	bool translateConstraints(spine38::SkeletonData &src, spine::SkeletonData &dst);

	bool translateEvents(spine38::SkeletonData &src, spine::SkeletonData &dst) {
		spine38::Vector<spine38::EventData *> &events = src.getEvents();
		for (size_t i = 0; i < events.size(); i++) {
			spine38::EventData *srcEvent = events[i];
			if (!srcEvent) return false;
			spine::EventData *dstEvent = new (__FILE__, __LINE__) spine::EventData(to43(srcEvent->getName()));
			spine::Event &setup = dstEvent->getSetupPose();
			setup.setInt(srcEvent->getIntValue());
			setup.setFloat(srcEvent->getFloatValue());
			setup.setString(to43(srcEvent->getStringValue()));
			setup.setVolume(srcEvent->getVolume());
			setup.setBalance(srcEvent->getBalance());
			dstEvent->setAudioPath(to43(srcEvent->getAudioPath()));
			dst.getEvents().add(dstEvent);
			_eventNames[toStd(srcEvent->getName())] = dstEvent;
		}
		return true;
	}

	bool translateSkins(spine38::SkeletonData &src, spine::SkeletonData &dst) {
		// pass 1: every attachment except linked meshes, which need their
		// source mesh translated first.
		for (size_t s = 0; s < src.getSkins().size(); s++) {
			spine38::Skin *srcSkin = src.getSkins()[s];
			if (!srcSkin) return false;
			spine::Skin *dstSkin = new (__FILE__, __LINE__) spine::Skin(to43(srcSkin->getName()));
			if (srcSkin == src.getDefaultSkin()) dst.setDefaultSkin(dstSkin);
			dst.getSkins().add(dstSkin);
			_skins.push_back(dstSkin);

			spine38::Skin::AttachmentMap::Entries entries = srcSkin->getAttachments();
			while (entries.hasNext()) {
				spine38::Skin::AttachmentMap::Entry &entry = entries.next();
				spine38::Attachment *srcAttachment = entry._attachment;
				if (!srcAttachment) return false;
				if (srcAttachment->getRTTI().instanceOf(spine38::MeshAttachment::rtti) &&
					((spine38::MeshAttachment *) srcAttachment)->getParentMesh())
					continue; // linked mesh, handled in pass 2
				spine::Attachment *dstAttachment = translateAttachment(srcAttachment, *dstSkin, to43(entry._name));
				if (!dstAttachment) return false;
				dstSkin->setAttachment(entry._slotIndex, to43(entry._name), dstAttachment);
				_attachments[srcAttachment] = dstAttachment;
			}
		}

		// pass 2: linked meshes, parents before children.
		bool progress = true;
		while (progress) {
			progress = false;
			for (size_t s = 0; s < src.getSkins().size(); s++) {
				spine38::Skin *srcSkin = src.getSkins()[s];
				spine::Skin *dstSkin = _skins[s];
				spine38::Skin::AttachmentMap::Entries entries = srcSkin->getAttachments();
				while (entries.hasNext()) {
					spine38::Skin::AttachmentMap::Entry &entry = entries.next();
					spine38::Attachment *srcAttachment = entry._attachment;
					if (!srcAttachment || !srcAttachment->getRTTI().instanceOf(spine38::MeshAttachment::rtti)) continue;
					spine38::MeshAttachment *srcMesh = (spine38::MeshAttachment *) srcAttachment;
					if (!srcMesh->getParentMesh()) continue;
					if (_attachments.count(srcMesh)) continue;
					if (!translateLinkedMesh(srcMesh, *dstSkin, (int) entry._slotIndex, to43(entry._name))) return false;
					progress = true;
				}
			}
		}
		return true;
	}

	spine::Attachment *translateAttachment(spine38::Attachment *src, spine::Skin &skin, const spine::String &placeholder);

	bool translateLinkedMesh(spine38::MeshAttachment *srcMesh, spine::Skin &skin, int slotIndex, const spine::String &placeholder);

	bool translateAnimations(spine38::SkeletonData &src, spine::SkeletonData &dst);

	bool translateTimeline(spine38::Timeline *srcTimeline, spine::Animation &dstAnimation);

	bool copyCurve(spine38::CurveTimeline *src, int frame, spine::CurveTimeline *dst, int &bezier, int valueCount, float time1,
				   const float *values1, float time2, const float *values2) {
		// the last frame has no curve segment in 3.8 data
		if ((size_t) frame + 1 >= src->getFrameCount()) return true;
		float type = src->getCurveType(frame);
		if (type == CURVE_STEPPED) {
			dst->setStepped(frame);
			return true;
		}
		if (type != CURVE_BEZIER) return true; // linear
		float c[4];
		if (!src->getControlPoints(frame, c)) {
			fail(_error, "Missing bezier control points in 3.8 data");
			return false;
		}
		// 3.8 stores control points in percent space (time percent, value
		// percent); 4.3 expects absolute time and absolute values.
		for (int v = 0; v < valueCount; v++) {
			float cx1 = time1 + (time2 - time1) * c[0];
			float cy1 = values1[v] + (values2[v] - values1[v]) * c[1];
			float cx2 = time1 + (time2 - time1) * c[2];
			float cy2 = values1[v] + (values2[v] - values1[v]) * c[3];
			dst->setBezier(bezier++, frame, v, time1, values1[v], cx1, cy1, cx2, cy2, time2, values2[v]);
		}
		return true;
	}

	int bezierCount(spine38::CurveTimeline *src, int valueCount) {
		// the 3.8 curves array only has entries for frames 0..count-2; the
		// last frame never carries a curve segment
		int count = 0;
		for (size_t i = 0; src->getFrameCount() > 0 && i + 1 < src->getFrameCount(); i++)
			if (src->getCurveType(i) == CURVE_BEZIER) count += valueCount;
		return count;
	}

public:
	spine::Atlas &_atlas;
	spine::String &_error;
	spine::AtlasAttachmentLoader _loader43;
	PlaceholderAttachmentLoader38 *_loader38 = new PlaceholderAttachmentLoader38();

	std::vector<spine::BoneData *> _bones;
	std::vector<spine::SlotData *> _slots;
	std::vector<spine::Skin *> _skins;
	std::map<std::string, spine::EventData *> _eventNames;
	std::map<spine38::Attachment *, spine::Attachment *> _attachments;
	size_t _ikCount = 0;
	size_t _transformCount = 0;
	size_t _pathCount = 0;
	int _bezier = 0;
};

bool Translator38::translateConstraints(spine38::SkeletonData &src, spine::SkeletonData &dst) {
	// 4.3 stores all constraints in one array and constraint timelines index
	// into it. Append IK first, then transform, then path to mirror the 3.8
	// application order; remember the counts to translate timeline indices.
	spine38::Vector<spine38::IkConstraintData *> &ikConstraints = src.getIkConstraints();
	for (size_t i = 0; i < ikConstraints.size(); i++) {
		spine38::IkConstraintData *srcData = ikConstraints[i];
		if (!srcData) return false;
		spine::IkConstraintData *dstData = new (__FILE__, __LINE__) spine::IkConstraintData(to43(srcData->getName()));
		spine::Array<spine::BoneData *> &dstBones = dstData->getBones();
		dstBones.setSize((int) srcData->getBones().size(), NULL);
		for (size_t b = 0; b < srcData->getBones().size(); b++)
			dstBones[b] = _bones[srcData->getBones()[b]->getIndex()];
		dstData->setTarget(*_bones[srcData->getTarget()->getIndex()]);
		spine::IkConstraintPose &setup = dstData->getSetupPose();
		setup.setMix(srcData->getMix());
		setup.setSoftness(srcData->getSoftness());
		setup.setBendDirection(srcData->getBendDirection());
		setup.setCompress(srcData->getCompress());
		setup.setStretch(srcData->getStretch());
		dst.getConstraints().add(dstData);
	}
	_ikCount = ikConstraints.size();

	spine38::Vector<spine38::TransformConstraintData *> &transformConstraints = src.getTransformConstraints();
	for (size_t i = 0; i < transformConstraints.size(); i++) {
		spine38::TransformConstraintData *srcData = transformConstraints[i];
		if (!srcData) return false;
		spine::TransformConstraintData *dstData = new (__FILE__, __LINE__) spine::TransformConstraintData(to43(srcData->getName()));
		spine::Array<spine::BoneData *> &dstBones = dstData->getBones();
		dstBones.setSize((int) srcData->getBones().size(), NULL);
		for (size_t b = 0; b < srcData->getBones().size(); b++)
			dstBones[b] = _bones[srcData->getBones()[b]->getIndex()];
		dstData->setSource(*_bones[srcData->getTarget()->getIndex()]);
		dstData->setLocalSource(srcData->isLocal());
		dstData->setLocalTarget(srcData->isLocal());
		dstData->setAdditive(srcData->isRelative());
		dstData->setClamp(false);
		dstData->setOffsetRotation(srcData->getOffsetRotation());
		dstData->setOffsetX(srcData->getOffsetX());
		dstData->setOffsetY(srcData->getOffsetY());
		dstData->setOffsetScaleX(srcData->getOffsetScaleX());
		dstData->setOffsetScaleY(srcData->getOffsetScaleY());
		dstData->setOffsetShearY(srcData->getOffsetShearY());
		spine::TransformConstraintPose &setup = dstData->getSetupPose();
		setup.setMixRotate(srcData->getRotateMix());
		setup.setMixX(srcData->getTranslateMix());
		setup.setMixY(srcData->getTranslateMix());
		setup.setMixScaleX(srcData->getScaleMix());
		setup.setMixScaleY(srcData->getScaleMix());
		setup.setMixShearY(srcData->getShearMix());

		// 3.8 animates four mixes (rotate, translate, scale, shear) while 4.3
		// routes each through a property pair. Register all six pairs so
		// timelines can drive any of them; pairs with a zero mix are skipped
		// by the 4.3 apply loop.
		for (int p = 0; p < 6; p++) {
			spine::FromProperty *from = NULL;
			spine::ToProperty *to = NULL;
			switch (p) {
				case 0:
					from = new (__FILE__, __LINE__) spine::FromRotate();
					to = new (__FILE__, __LINE__) spine::ToRotate();
					break;
				case 1:
					from = new (__FILE__, __LINE__) spine::FromX();
					to = new (__FILE__, __LINE__) spine::ToX();
					break;
				case 2:
					from = new (__FILE__, __LINE__) spine::FromY();
					to = new (__FILE__, __LINE__) spine::ToY();
					break;
				case 3:
					from = new (__FILE__, __LINE__) spine::FromScaleX();
					to = new (__FILE__, __LINE__) spine::ToScaleX();
					break;
				case 4:
					from = new (__FILE__, __LINE__) spine::FromScaleY();
					to = new (__FILE__, __LINE__) spine::ToScaleY();
					break;
				default:
					from = new (__FILE__, __LINE__) spine::FromShearY();
					to = new (__FILE__, __LINE__) spine::ToShearY();
					break;
			}
			from->_offset = 0;
			to->_offset = 0;
			to->_max = 0;
			to->_scale = 1;
			from->_to.add(to);
			dstData->getProperties().add(from);
		}
		dst.getConstraints().add(dstData);
	}
	_transformCount = transformConstraints.size();

	spine38::Vector<spine38::PathConstraintData *> &pathConstraints = src.getPathConstraints();
	for (size_t i = 0; i < pathConstraints.size(); i++) {
		spine38::PathConstraintData *srcData = pathConstraints[i];
		if (!srcData) return false;
		spine::PathConstraintData *dstData = new (__FILE__, __LINE__) spine::PathConstraintData(to43(srcData->getName()));
		spine::Array<spine::BoneData *> &dstBones = dstData->getBones();
		dstBones.setSize((int) srcData->getBones().size(), NULL);
		for (size_t b = 0; b < srcData->getBones().size(); b++)
			dstBones[b] = _bones[srcData->getBones()[b]->getIndex()];
		dstData->setSlot(*_slots[srcData->getTarget()->getIndex()]);
		dstData->setPositionMode((spine::PositionMode) srcData->getPositionMode());
		dstData->setSpacingMode((spine::SpacingMode) srcData->getSpacingMode());
		dstData->setRotateMode((spine::RotateMode) srcData->getRotateMode());
		dstData->setOffsetRotation(srcData->getOffsetRotation());
		spine::PathConstraintPose &setup = dstData->getSetupPose();
		setup.setPosition(srcData->getPosition());
		setup.setSpacing(srcData->getSpacing());
		setup.setMixRotate(srcData->getRotateMix());
		setup.setMixX(srcData->getTranslateMix());
		setup.setMixY(srcData->getTranslateMix());
		dst.getConstraints().add(dstData);
	}
	_pathCount = pathConstraints.size();
	return true;
}

spine::Attachment *Translator38::translateAttachment(spine38::Attachment *src, spine::Skin &skin, const spine::String &placeholder) {
	if (src->getRTTI().isExactly(spine38::RegionAttachment::rtti)) {
		spine38::RegionAttachment *srcRegion = (spine38::RegionAttachment *) src;
		spine::String path = to43(attachmentPath(src));
		if (!_atlas.findRegion(path)) path = to43(srcRegion->getName());
		if (!_atlas.findRegion(path)) {
			fail(_error, "Region not found in atlas: ", path.buffer());
			return NULL;
		}
		spine::RegionAttachment *dst = _loader43.newRegionAttachment(skin, placeholder, to43(src->getName()), path,
																	 new (__FILE__, __LINE__) spine::Sequence(1, false));
		dst->setPath(path);
		dst->setX(srcRegion->getX());
		dst->setY(srcRegion->getY());
		dst->setScaleX(srcRegion->getScaleX());
		dst->setScaleY(srcRegion->getScaleY());
		dst->setRotation(srcRegion->getRotation());
		dst->setWidth(srcRegion->getWidth());
		dst->setHeight(srcRegion->getHeight());
		spine38::Color &c = srcRegion->getColor();
		dst->getColor().set(c.r, c.g, c.b, c.a);
		dst->updateSequence();
		return dst;
	}
	if (src->getRTTI().isExactly(spine38::MeshAttachment::rtti)) {
		spine38::MeshAttachment *srcMesh = (spine38::MeshAttachment *) src;
		spine::String path = to43(attachmentPath(src));
		if (!_atlas.findRegion(path)) path = to43(srcMesh->getName());
		if (!_atlas.findRegion(path)) {
			fail(_error, "Region not found in atlas: ", path.buffer());
			return NULL;
		}
		spine::MeshAttachment *dst = _loader43.newMeshAttachment(skin, placeholder, to43(src->getName()), path,
																 new (__FILE__, __LINE__) spine::Sequence(1, false));
		copyMeshData(srcMesh, dst);
		spine38::Color &c = srcMesh->getColor();
		dst->getColor().set(c.r, c.g, c.b, c.a);
		dst->updateSequence();
		return dst;
	}
	if (src->getRTTI().isExactly(spine38::BoundingBoxAttachment::rtti)) {
		spine38::BoundingBoxAttachment *srcBox = (spine38::BoundingBoxAttachment *) src;
		spine::BoundingBoxAttachment *dst = _loader43.newBoundingBoxAttachment(skin, placeholder, to43(src->getName()));
		dst->setWorldVerticesLength(srcBox->getWorldVerticesLength());
		copyFloatArray(srcBox->getVertices(), dst->getVertices());
		copyBoneArray(srcBox->getBones(), dst->getBones());
		return dst;
	}
	if (src->getRTTI().isExactly(spine38::PathAttachment::rtti)) {
		spine38::PathAttachment *srcPath = (spine38::PathAttachment *) src;
		spine::PathAttachment *dst = _loader43.newPathAttachment(skin, placeholder, to43(src->getName()));
		dst->setWorldVerticesLength(srcPath->getWorldVerticesLength());
		copyFloatArray(srcPath->getVertices(), dst->getVertices());
		copyBoneArray(srcPath->getBones(), dst->getBones());
		dst->setClosed(srcPath->isClosed());
		dst->setConstantSpeed(srcPath->isConstantSpeed());
		copyFloatArray(srcPath->getLengths(), dst->getLengths());
		return dst;
	}
	if (src->getRTTI().isExactly(spine38::PointAttachment::rtti)) {
		spine38::PointAttachment *srcPoint = (spine38::PointAttachment *) src;
		spine::PointAttachment *dst = _loader43.newPointAttachment(skin, placeholder, to43(src->getName()));
		dst->setX(srcPoint->getX());
		dst->setY(srcPoint->getY());
		dst->setRotation(srcPoint->getRotation());
		return dst;
	}
	if (src->getRTTI().isExactly(spine38::ClippingAttachment::rtti)) {
		spine38::ClippingAttachment *srcClip = (spine38::ClippingAttachment *) src;
		spine::ClippingAttachment *dst = _loader43.newClippingAttachment(skin, placeholder, to43(src->getName()));
		if (srcClip->getEndSlot()) dst->setEndSlot(_slots[srcClip->getEndSlot()->getIndex()]);
		return dst;
	}
	fail(_error, "Unknown 3.8 attachment type: ", src->getName().buffer());
	return NULL;
}

bool Translator38::translateLinkedMesh(spine38::MeshAttachment *srcMesh, spine::Skin &skin, int slotIndex, const spine::String &placeholder) {
	if (_attachments.count(srcMesh)) return true;

	// resolve the root of the chain; it always carries the real vertex data
	spine38::MeshAttachment *root38 = srcMesh;
	while (root38->getParentMesh()) root38 = root38->getParentMesh();
	if (!_attachments.count(root38)) {
		fail(_error, "Linked mesh source not found: ", srcMesh->getName().buffer());
		return false;
	}

	spine::String path = to43(attachmentPath(srcMesh));
	if (!_atlas.findRegion(path)) path = to43(srcMesh->getName());
	if (!_atlas.findRegion(path)) {
		fail(_error, "Region not found in atlas: ", path.buffer());
		return false;
	}
	spine::MeshAttachment *dst = _loader43.newMeshAttachment(skin, placeholder, to43(srcMesh->getName()), path,
															 new (__FILE__, __LINE__) spine::Sequence(1, false));
	copyMeshData(root38, dst);
	spine38::Color &c = srcMesh->getColor();
	dst->getColor().set(c.r, c.g, c.b, c.a);
	dst->updateSequence();

	// mirror the 3.8 source chain and deform attachment
	spine38::MeshAttachment *parent38 = srcMesh->getParentMesh();
	if (!_attachments.count(parent38)) {
		fail(_error, "Linked mesh parent not translated: ", parent38->getName().buffer());
		return false;
	}
	spine::Attachment *parent43 = _attachments[parent38];
	dst->setSourceMesh(static_cast<spine::MeshAttachment *>(parent43));
	if (srcMesh->getDeformAttachment() == parent38)
		dst->setTimelineAttachment(parent43->getRTTI().instanceOf(spine::VertexAttachment::rtti)
								   ? static_cast<spine::VertexAttachment *>(parent43)
								   : dst);
	else
		dst->setTimelineAttachment(dst);

	skin.setAttachment((size_t) slotIndex, placeholder, dst);
	_attachments[srcMesh] = dst;
	return true;
}

bool Translator38::translateTimeline(spine38::Timeline *srcTimeline, spine::Animation &dstAnimation) {
	_bezier = 0;
	if (srcTimeline->getRTTI().isExactly(spine38::RotateTimeline::rtti)) {
		spine38::RotateTimeline *src = (spine38::RotateTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::RotateTimeline *dst = new (__FILE__, __LINE__) spine::RotateTimeline(frameCount, bezierCount(src, 1), src->getBoneIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 2];
			float value = src->getFrames()[i * 2 + 1];
			dst->setFrame(i, time, value);
			float v1[1] = {value};
			bool next = i + 1 < frameCount;
			float v2[1] = {next ? src->getFrames()[(i + 1) * 2 + 1] : value};
			if (!copyCurve(src, i, dst, _bezier, 1, time, v1, next ? src->getFrames()[(i + 1) * 2] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::ScaleTimeline::rtti)) {
		spine38::ScaleTimeline *src = (spine38::ScaleTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::ScaleTimeline *dst = new (__FILE__, __LINE__) spine::ScaleTimeline(frameCount, bezierCount(src, 2), src->getBoneIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 3];
			float x = src->getFrames()[i * 3 + 1];
			float y = src->getFrames()[i * 3 + 2];
			dst->setFrame(i, time, x, y);
			float v1[2] = {x, y};
			bool next = i + 1 < frameCount;
			float v2[2] = {next ? src->getFrames()[(i + 1) * 3 + 1] : x, next ? src->getFrames()[(i + 1) * 3 + 2] : y};
			if (!copyCurve(src, i, dst, _bezier, 2, time, v1, next ? src->getFrames()[(i + 1) * 3] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::ShearTimeline::rtti)) {
		spine38::ShearTimeline *src = (spine38::ShearTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::ShearTimeline *dst = new (__FILE__, __LINE__) spine::ShearTimeline(frameCount, bezierCount(src, 2), src->getBoneIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 3];
			float x = src->getFrames()[i * 3 + 1];
			float y = src->getFrames()[i * 3 + 2];
			dst->setFrame(i, time, x, y);
			float v1[2] = {x, y};
			bool next = i + 1 < frameCount;
			float v2[2] = {next ? src->getFrames()[(i + 1) * 3 + 1] : x, next ? src->getFrames()[(i + 1) * 3 + 2] : y};
			if (!copyCurve(src, i, dst, _bezier, 2, time, v1, next ? src->getFrames()[(i + 1) * 3] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::TranslateTimeline::rtti)) {
		spine38::TranslateTimeline *src = (spine38::TranslateTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::TranslateTimeline *dst = new (__FILE__, __LINE__) spine::TranslateTimeline(frameCount, bezierCount(src, 2), src->getBoneIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 3];
			float x = src->getFrames()[i * 3 + 1];
			float y = src->getFrames()[i * 3 + 2];
			dst->setFrame(i, time, x, y);
			float v1[2] = {x, y};
			bool next = i + 1 < frameCount;
			float v2[2] = {next ? src->getFrames()[(i + 1) * 3 + 1] : x, next ? src->getFrames()[(i + 1) * 3 + 2] : y};
			if (!copyCurve(src, i, dst, _bezier, 2, time, v1, next ? src->getFrames()[(i + 1) * 3] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::ColorTimeline::rtti)) {
		spine38::ColorTimeline *src = (spine38::ColorTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::RGBATimeline *dst = new (__FILE__, __LINE__) spine::RGBATimeline(frameCount, bezierCount(src, 4), src->getSlotIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 5];
			float v1[4] = {src->getFrames()[i * 5 + 1], src->getFrames()[i * 5 + 2], src->getFrames()[i * 5 + 3],
						   src->getFrames()[i * 5 + 4]};
			dst->setFrame(i, time, v1[0], v1[1], v1[2], v1[3]);
			bool next = i + 1 < frameCount;
			float v2[4];
			for (int c = 0; c < 4; c++) v2[c] = next ? src->getFrames()[(i + 1) * 5 + 1 + c] : v1[c];
			if (!copyCurve(src, i, dst, _bezier, 4, time, v1, next ? src->getFrames()[(i + 1) * 5] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::TwoColorTimeline::rtti)) {
		spine38::TwoColorTimeline *src = (spine38::TwoColorTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::RGBA2Timeline *dst = new (__FILE__, __LINE__) spine::RGBA2Timeline(frameCount, bezierCount(src, 7), src->getSlotIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 8];
			float v1[7] = {src->getFrames()[i * 8 + 1], src->getFrames()[i * 8 + 2], src->getFrames()[i * 8 + 3],
						   src->getFrames()[i * 8 + 4], src->getFrames()[i * 8 + 5], src->getFrames()[i * 8 + 6],
						   src->getFrames()[i * 8 + 7]};
			dst->setFrame(i, time, v1[0], v1[1], v1[2], v1[3], v1[4], v1[5], v1[6]);
			bool next = i + 1 < frameCount;
			float v2[7];
			for (int c = 0; c < 7; c++) v2[c] = next ? src->getFrames()[(i + 1) * 8 + 1 + c] : v1[c];
			if (!copyCurve(src, i, dst, _bezier, 7, time, v1, next ? src->getFrames()[(i + 1) * 8] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::AttachmentTimeline::rtti)) {
		spine38::AttachmentTimeline *src = (spine38::AttachmentTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::AttachmentTimeline *dst = new (__FILE__, __LINE__) spine::AttachmentTimeline(frameCount, src->getSlotIndex());
		for (int i = 0; i < frameCount; i++) {
			dst->setFrame(i, src->getFrames()[i], to43(src->getAttachmentNames()[i]));
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::IkConstraintTimeline::rtti)) {
		spine38::IkConstraintTimeline *src = (spine38::IkConstraintTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::IkConstraintTimeline *dst = new (__FILE__, __LINE__) spine::IkConstraintTimeline(frameCount, bezierCount(src, 2),
																								(int) src->getIkConstraintIndex());
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 6];
			float mix = src->getFrames()[i * 6 + 1];
			float softness = src->getFrames()[i * 6 + 2];
			int bend = (int) src->getFrames()[i * 6 + 3];
			bool compress = src->getFrames()[i * 6 + 4] != 0;
			bool stretch = src->getFrames()[i * 6 + 5] != 0;
			dst->setFrame(i, time, mix, softness, bend, compress, stretch);
			float v1[2] = {mix, softness};
			bool next = i + 1 < frameCount;
			float v2[2] = {next ? src->getFrames()[(i + 1) * 6 + 1] : mix, next ? src->getFrames()[(i + 1) * 6 + 2] : softness};
			if (!copyCurve(src, i, dst, _bezier, 2, time, v1, next ? src->getFrames()[(i + 1) * 6] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::TransformConstraintTimeline::rtti)) {
		spine38::TransformConstraintTimeline *src = (spine38::TransformConstraintTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		int constraintIndex = _ikCount + (int) src->getTransformConstraintIndex();
		spine::TransformConstraintTimeline *dst = new (__FILE__, __LINE__) spine::TransformConstraintTimeline(frameCount,
																											  bezierCount(src, 6),
																											  constraintIndex);
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 5];
			float rotate = src->getFrames()[i * 5 + 1];
			float translate = src->getFrames()[i * 5 + 2];
			float scale = src->getFrames()[i * 5 + 3];
			float shear = src->getFrames()[i * 5 + 4];
			dst->setFrame(i, time, rotate, translate, translate, scale, scale, shear);
			float v1[6] = {rotate, translate, translate, scale, scale, shear};
			bool next = i + 1 < frameCount;
			float v2[6];
			for (int c = 0; c < 6; c++) {
				// collapse duplicated translate/scale values back to the
				// single 3.8 value they were animated with
				int sourceOffset = c == 0 ? 1 : (c == 3 ? 3 : (c == 5 ? 4 : (c == 1 || c == 2 ? 2 : 3)));
				v2[c] = next ? src->getFrames()[(i + 1) * 5 + sourceOffset] : v1[c];
			}
			if (!copyCurve(src, i, dst, _bezier, 6, time, v1, next ? src->getFrames()[(i + 1) * 5] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::PathConstraintPositionTimeline::rtti)) {
		spine38::PathConstraintPositionTimeline *src = (spine38::PathConstraintPositionTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		int constraintIndex = _ikCount + _transformCount + (int) src->getPathConstraintIndex();
		spine::PathConstraintPositionTimeline *dst = new (__FILE__, __LINE__) spine::PathConstraintPositionTimeline(frameCount,
																													bezierCount(src, 1),
																													constraintIndex);
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 2];
			float value = src->getFrames()[i * 2 + 1];
			dst->setFrame(i, time, value);
			float v1[1] = {value};
			bool next = i + 1 < frameCount;
			float v2[1] = {next ? src->getFrames()[(i + 1) * 2 + 1] : value};
			if (!copyCurve(src, i, dst, _bezier, 1, time, v1, next ? src->getFrames()[(i + 1) * 2] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::PathConstraintSpacingTimeline::rtti)) {
		spine38::PathConstraintSpacingTimeline *src = (spine38::PathConstraintSpacingTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		int constraintIndex = _ikCount + _transformCount + (int) src->getPathConstraintIndex();
		spine::PathConstraintSpacingTimeline *dst = new (__FILE__, __LINE__) spine::PathConstraintSpacingTimeline(frameCount,
																												  bezierCount(src, 1),
																												  constraintIndex);
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 2];
			float value = src->getFrames()[i * 2 + 1];
			dst->setFrame(i, time, value);
			float v1[1] = {value};
			bool next = i + 1 < frameCount;
			float v2[1] = {next ? src->getFrames()[(i + 1) * 2 + 1] : value};
			if (!copyCurve(src, i, dst, _bezier, 1, time, v1, next ? src->getFrames()[(i + 1) * 2] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::PathConstraintMixTimeline::rtti)) {
		spine38::PathConstraintMixTimeline *src = (spine38::PathConstraintMixTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		int constraintIndex = _ikCount + _transformCount + (int) src->getPathConstraintIndex();
		spine::PathConstraintMixTimeline *dst = new (__FILE__, __LINE__) spine::PathConstraintMixTimeline(frameCount, bezierCount(src, 3),
																										  constraintIndex);
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i * 3];
			float rotate = src->getFrames()[i * 3 + 1];
			float translate = src->getFrames()[i * 3 + 2];
			dst->setFrame(i, time, rotate, translate, translate);
			float v1[3] = {rotate, translate, translate};
			bool next = i + 1 < frameCount;
			float v2[3] = {next ? src->getFrames()[(i + 1) * 3 + 1] : rotate, next ? src->getFrames()[(i + 1) * 3 + 2] : translate,
						   next ? src->getFrames()[(i + 1) * 3 + 2] : translate};
			if (!copyCurve(src, i, dst, _bezier, 3, time, v1, next ? src->getFrames()[(i + 1) * 3] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::DeformTimeline::rtti)) {
		spine38::DeformTimeline *src = (spine38::DeformTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine38::Attachment *srcAttachment = src->getAttachment();
		if (!srcAttachment || !_attachments.count(srcAttachment)) {
			fail(_error, "Deform timeline attachment not found");
			return false;
		}
		spine::Attachment *translated = _attachments[srcAttachment];
		spine::DeformTimeline *dst = new (__FILE__, __LINE__) spine::DeformTimeline(frameCount, bezierCount(src, 1), src->getSlotIndex(),
																					*static_cast<spine::VertexAttachment *>(translated));
		for (int i = 0; i < frameCount; i++) {
			float time = src->getFrames()[i];
			spine::Array<float> vertices;
			spine38::Vector<float> &srcVertices = src->getVertices()[i];
			vertices.setSize((int) srcVertices.size(), 0);
			for (size_t v = 0; v < srcVertices.size(); v++) vertices[v] = srcVertices[v];
			dst->setFrame(i, time, vertices);
			// deform curves interpolate in normalized percent space on both
			// sides: value1 = 0, value2 = 1, matching 4.3's own binary reader
			float v1[1] = {0};
			float v2[1] = {1};
			bool next = i + 1 < frameCount;
			if (!copyCurve(src, i, dst, _bezier, 1, time, v1, next ? src->getFrames()[i + 1] : time, v2)) return false;
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::EventTimeline::rtti)) {
		spine38::EventTimeline *src = (spine38::EventTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::EventTimeline *dst = new (__FILE__, __LINE__) spine::EventTimeline(frameCount);
		for (int i = 0; i < frameCount; i++) {
			spine38::Event *srcEvent = src->getEvents()[i];
			if (!srcEvent) return false;
			std::map<std::string, spine::EventData *>::iterator it = _eventNames.find(toStd(srcEvent->getData().getName()));
			if (it == _eventNames.end()) {
				fail(_error, "Event data not found: ", srcEvent->getData().getName().buffer());
				return false;
			}
			spine::Event *dstEvent = new (__FILE__, __LINE__) spine::Event(srcEvent->getTime(), *it->second);
			dstEvent->setInt(srcEvent->getIntValue());
			dstEvent->setFloat(srcEvent->getFloatValue());
			dstEvent->setString(to43(srcEvent->getStringValue()));
			dstEvent->setVolume(srcEvent->getVolume());
			dstEvent->setBalance(srcEvent->getBalance());
			dst->setFrame(i, *dstEvent);
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	if (srcTimeline->getRTTI().isExactly(spine38::DrawOrderTimeline::rtti)) {
		spine38::DrawOrderTimeline *src = (spine38::DrawOrderTimeline *) srcTimeline;
		int frameCount = (int) src->getFrameCount();
		spine::DrawOrderTimeline *dst = new (__FILE__, __LINE__) spine::DrawOrderTimeline(frameCount);
		for (int i = 0; i < frameCount; i++) {
			spine38::Vector<int> &srcDrawOrder = src->getDrawOrders()[i];
			if (srcDrawOrder.size() == 0) {
				dst->setFrame(i, src->getFrames()[i], NULL);
			} else {
				spine::Array<int> drawOrder;
				drawOrder.setSize((int) srcDrawOrder.size(), 0);
				for (size_t s = 0; s < srcDrawOrder.size(); s++) drawOrder[s] = srcDrawOrder[s];
				dst->setFrame(i, src->getFrames()[i], &drawOrder);
			}
		}
		dstAnimation.getTimelines().add(dst);
		return true;
	}
	fail(_error, "Unknown 3.8 timeline type");
	return false;
}

bool Translator38::translateAnimations(spine38::SkeletonData &src, spine::SkeletonData &dst) {
	spine38::Vector<spine38::Animation *> &animations = src.getAnimations();
	for (size_t i = 0; i < animations.size(); i++) {
		spine38::Animation *srcAnimation = animations[i];
		if (!srcAnimation) return false;
		spine::Animation *dstAnimation = new (__FILE__, __LINE__) spine::Animation(to43(srcAnimation->getName()));
		spine38::Vector<spine38::Timeline *> &timelines = srcAnimation->getTimelines();
		for (size_t t = 0; t < timelines.size(); t++) {
			if (!translateTimeline(timelines[t], *dstAnimation)) return false;
		}
		dstAnimation->setDuration(srcAnimation->getDuration());
		dst.getAnimations().add(dstAnimation);
	}
	return true;
}

} // namespace

spine::SkeletonData *load_binary_38(const uint8_t *data, size_t length, spine::Atlas &atlas, char *errorOut, size_t errorOutSize) {
	spine::String error;
	Translator38 translator(atlas, error);
	spine38::SkeletonData *src = NULL;
	spine::SkeletonData *result = NULL;
	{
		spine38::SkeletonBinary binary(translator._loader38);
		src = binary.readSkeletonData(data, (const int) length);
		if (!src) {
			fail(error, "Failed to parse 3.8 skeleton binary: ", binary.getError().buffer());
		}
	}
	if (src) {
		result = translator.translate(*src);
		delete src;
	}
	if (errorOut && errorOutSize) {
		const char *msg = error.buffer();
		strncpy(errorOut, msg ? msg : "", errorOutSize - 1);
		errorOut[errorOutSize - 1] = '\0';
	}
	return result;
}

spine::SkeletonData *load_json_38(const char *json, spine::Atlas &atlas, char *errorOut, size_t errorOutSize) {
	spine::String error;
	Translator38 translator(atlas, error);
	spine38::SkeletonData *src = NULL;
	spine::SkeletonData *result = NULL;
	{
		spine38::SkeletonJson jsonReader(translator._loader38);
		src = jsonReader.readSkeletonData(json);
		if (!src) {
			fail(error, "Failed to parse 3.8 skeleton json: ", jsonReader.getError().buffer());
		}
	}
	if (src) {
		result = translator.translate(*src);
		delete src;
	}
	if (errorOut && errorOutSize) {
		const char *msg = error.buffer();
		strncpy(errorOut, msg ? msg : "", errorOutSize - 1);
		errorOut[errorOutSize - 1] = '\0';
	}
	return result;
}

} // namespace Spine38Loader

#endif // SPINE_GODOT_EXTENSION
