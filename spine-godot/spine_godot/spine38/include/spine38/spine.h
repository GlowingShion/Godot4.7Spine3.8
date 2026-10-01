/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated January 1, 2020. Replaces all prior versions.
 *
 * Copyright (c) 2013-2020, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef SPINE38_SPINE_H_
#define SPINE38_SPINE_H_

#include <spine38/Animation.h>
#include <spine38/AnimationState.h>
#include <spine38/AnimationStateData.h>
#include <spine38/Atlas.h>
#include <spine38/AtlasAttachmentLoader.h>
#include <spine38/Attachment.h>
#include <spine38/AttachmentLoader.h>
#include <spine38/AttachmentTimeline.h>
#include <spine38/AttachmentType.h>
#include <spine38/BlendMode.h>
#include <spine38/Bone.h>
#include <spine38/BoneData.h>
#include <spine38/BoundingBoxAttachment.h>
#include <spine38/ClippingAttachment.h>
#include <spine38/Color.h>
#include <spine38/ColorTimeline.h>
#include <spine38/ConstraintData.h>
#include <spine38/ContainerUtil.h>
#include <spine38/CurveTimeline.h>
#include <spine38/DeformTimeline.h>
#include <spine38/DrawOrderTimeline.h>
#include <spine38/Event.h>
#include <spine38/EventData.h>
#include <spine38/EventTimeline.h>
#include <spine38/Extension.h>
#include <spine38/HashMap.h>
#include <spine38/HasRendererObject.h>
#include <spine38/IkConstraint.h>
#include <spine38/IkConstraintData.h>
#include <spine38/IkConstraintTimeline.h>
#include <spine38/Json.h>
#include <spine38/LinkedMesh.h>
#include <spine38/MathUtil.h>
#include <spine38/MeshAttachment.h>
#include <spine38/MixBlend.h>
#include <spine38/MixDirection.h>
#include <spine38/PathAttachment.h>
#include <spine38/PathConstraint.h>
#include <spine38/PathConstraintData.h>
#include <spine38/PathConstraintMixTimeline.h>
#include <spine38/PathConstraintPositionTimeline.h>
#include <spine38/PathConstraintSpacingTimeline.h>
#include <spine38/PointAttachment.h>
#include <spine38/Pool.h>
#include <spine38/PositionMode.h>
#include <spine38/RegionAttachment.h>
#include <spine38/RotateMode.h>
#include <spine38/RotateTimeline.h>
#include <spine38/RTTI.h>
#include <spine38/ScaleTimeline.h>
#include <spine38/ShearTimeline.h>
#include <spine38/Skeleton.h>
#include <spine38/SkeletonBinary.h>
#include <spine38/SkeletonBounds.h>
#include <spine38/SkeletonClipping.h>
#include <spine38/SkeletonData.h>
#include <spine38/SkeletonJson.h>
#include <spine38/Skin.h>
#include <spine38/Slot.h>
#include <spine38/SlotData.h>
#include <spine38/SpacingMode.h>
#include <spine38/SpineObject.h>
#include <spine38/SpineString.h>
#include <spine38/TextureLoader.h>
#include <spine38/Timeline.h>
#include <spine38/TimelineType.h>
#include <spine38/TransformConstraint.h>
#include <spine38/TransformConstraintData.h>
#include <spine38/TransformConstraintTimeline.h>
#include <spine38/TransformMode.h>
#include <spine38/TranslateTimeline.h>
#include <spine38/Triangulator.h>
#include <spine38/TwoColorTimeline.h>
#include <spine38/Updatable.h>
#include <spine38/Vector.h>
#include <spine38/VertexAttachment.h>
#include <spine38/VertexEffect.h>
#include <spine38/Vertices.h>

#endif
