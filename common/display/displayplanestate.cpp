﻿/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "displayplanestate.h"
#include "hwctrace.h"

namespace hwcomposer {

DisplayPlaneState::DisplayPlaneState(DisplayPlane *plane, OverlayLayer *layer,
                                     uint32_t index) {
  private_data_ = std::make_shared<DisplayPlanePrivateState>();
  private_data_->source_layers_.emplace_back(index);
  private_data_->display_frame_ = layer->GetDisplayFrame();
  private_data_->check_display_scalar_ = true;
  private_data_->check_downscaling_ = true;
  private_data_->source_crop_ = layer->GetSourceCrop();
  if (layer->IsCursorLayer()) {
    private_data_->type_ = DisplayPlanePrivateState::PlaneType::kCursor;
    private_data_->has_cursor_layer_ = true;
  }

  plane->SetInUse(true);
  private_data_->plane_ = plane;
  private_data_->layer_ = layer;
}

void DisplayPlaneState::CopyState(DisplayPlaneState &state) {
  private_data_ = state.private_data_;
  // We don't copy recycled_surface_ state as this
  // should be determined in DisplayQueue for every frame.
}

const HwcRect<int> &DisplayPlaneState::GetDisplayFrame() const {
  return private_data_->display_frame_;
}

const HwcRect<float> &DisplayPlaneState::GetSourceCrop() const {
  return private_data_->source_crop_;
}

void DisplayPlaneState::AddLayer(const OverlayLayer *layer) {
  const HwcRect<int> &display_frame = layer->GetDisplayFrame();
  HwcRect<int> &target_display_frame = private_data_->display_frame_;
  target_display_frame.left =
      std::min(target_display_frame.left, display_frame.left);
  target_display_frame.top =
      std::min(target_display_frame.top, display_frame.top);
  target_display_frame.right =
      std::max(target_display_frame.right, display_frame.right);
  target_display_frame.bottom =
      std::max(target_display_frame.bottom, display_frame.bottom);

  HwcRect<float> &target_source_crop = private_data_->source_crop_;
  const HwcRect<float> &source_crop = layer->GetSourceCrop();
  target_source_crop.left = std::min(target_source_crop.left, source_crop.left);
  target_source_crop.top = std::min(target_source_crop.top, source_crop.top);
  target_source_crop.right =
      std::max(target_source_crop.right, source_crop.right);
  target_source_crop.bottom =
      std::max(target_source_crop.bottom, source_crop.bottom);

  private_data_->source_layers_.emplace_back(layer->GetZorder());

  private_data_->state_ = DisplayPlanePrivateState::State::kRender;

  if (!private_data_->has_cursor_layer_)
    private_data_->has_cursor_layer_ = layer->IsCursorLayer();

  if (private_data_->source_layers_.size() == 1 &&
      private_data_->has_cursor_layer_) {
    private_data_->type_ = DisplayPlanePrivateState::PlaneType::kCursor;
  } else {
    // TODO: Add checks for Video type once our
    // Media backend can support compositing more
    // than one layer together.
    private_data_->type_ = DisplayPlanePrivateState::PlaneType::kNormal;
    private_data_->apply_effects_ = false;
  }

  refresh_needed_ = true;
}

void DisplayPlaneState::ResetLayers(const std::vector<OverlayLayer> &layers,
                                    size_t remove_index) {
  const std::vector<size_t> &current_layers = private_data_->source_layers_;
  std::vector<size_t> source_layers;
  bool had_cursor = private_data_->has_cursor_layer_;
  private_data_->has_cursor_layer_ = false;
  bool initialized = false;
  HwcRect<int> target_display_frame;
  HwcRect<float> target_source_crop;
  bool has_video = false;
  for (const size_t &index : current_layers) {
    if (index >= remove_index) {
#ifdef SURFACE_TRACING
      ISURFACETRACE("Reset breaks index: %d remove_index %d \n", index,
                    remove_index);
#endif
      break;
    }

    const OverlayLayer &layer = layers.at(index);
    bool is_cursor = layer.IsCursorLayer();
    if (!had_cursor && is_cursor) {
      continue;
    }

    if (is_cursor) {
      private_data_->has_cursor_layer_ = true;
    } else if (!has_video) {
      has_video = layer.IsVideoLayer();
    }

    const HwcRect<int> &df = layer.GetDisplayFrame();
    const HwcRect<float> &source_crop = layer.GetSourceCrop();
    if (!initialized) {
      target_display_frame = df;
      target_source_crop = source_crop;
      initialized = true;
    } else {
      target_display_frame.left = std::min(target_display_frame.left, df.left);
      target_display_frame.top = std::min(target_display_frame.top, df.top);
      target_display_frame.right =
          std::max(target_display_frame.right, df.right);
      target_display_frame.bottom =
          std::max(target_display_frame.bottom, df.bottom);

      target_source_crop.left =
          std::min(target_source_crop.left, source_crop.left);
      target_source_crop.top =
          std::min(target_source_crop.top, source_crop.top);
      target_source_crop.right =
          std::max(target_source_crop.right, source_crop.right);
      target_source_crop.bottom =
          std::max(target_source_crop.bottom, source_crop.bottom);
    }
#ifdef SURFACE_TRACING
    ISURFACETRACE("Reset adds index: %d \n", layer.GetZorder());
#endif
    source_layers.emplace_back(layer.GetZorder());
  }

  if (source_layers.empty()) {
    private_data_->source_layers_.swap(source_layers);
    return;
  }

#ifdef SURFACE_TRACING
  ISURFACETRACE(
      "Reset called has_video: %d Source Layers Size: %d Previous Source "
      "Layers Size: %d Has Cursor: %d Total Layers Size: %d \n",
      has_video, source_layers.size(), current_layers.size(),
      private_data_->has_cursor_layer_, layers.size());
#endif

  private_data_->source_layers_.swap(source_layers);
  private_data_->display_frame_ = target_display_frame;
  private_data_->source_crop_ = target_source_crop;
  private_data_->check_display_scalar_ = true;
  private_data_->check_downscaling_ = true;

  if (private_data_->source_layers_.size() == 1) {
    if (private_data_->has_cursor_layer_) {
      private_data_->type_ = DisplayPlanePrivateState::PlaneType::kCursor;
    } else if (has_video) {
      private_data_->type_ = DisplayPlanePrivateState::PlaneType::kVideo;
    } else {
      private_data_->type_ = DisplayPlanePrivateState::PlaneType::kNormal;
    }
  } else {
    private_data_->type_ = DisplayPlanePrivateState::PlaneType::kNormal;
  }

  std::vector<CompositionRegion>().swap(private_data_->composition_region_);
  refresh_needed_ = true;
}

void DisplayPlaneState::UpdateDisplayFrame(const HwcRect<int> &display_frame) {
  HwcRect<int> &target_display_frame = private_data_->display_frame_;
  target_display_frame.left =
      std::min(target_display_frame.left, display_frame.left);
  target_display_frame.top =
      std::min(target_display_frame.top, display_frame.top);
  target_display_frame.right =
      std::max(target_display_frame.right, display_frame.right);
  target_display_frame.bottom =
      std::max(target_display_frame.bottom, display_frame.bottom);
  private_data_->check_display_scalar_ = true;
  private_data_->check_downscaling_ = true;
  refresh_needed_ = true;
}

void DisplayPlaneState::UpdateSourceCrop(const HwcRect<float> &source_crop) {
  HwcRect<float> &target_source_crop = private_data_->source_crop_;
  target_source_crop.left = std::min(target_source_crop.left, source_crop.left);
  target_source_crop.top = std::min(target_source_crop.top, source_crop.top);
  target_source_crop.right =
      std::max(target_source_crop.right, source_crop.right);
  target_source_crop.bottom =
      std::max(target_source_crop.bottom, source_crop.bottom);
  private_data_->check_display_scalar_ = true;
  private_data_->check_downscaling_ = true;
  refresh_needed_ = true;
}

void DisplayPlaneState::ForceGPURendering() {
  private_data_->state_ = DisplayPlanePrivateState::State::kRender;
}

void DisplayPlaneState::DisableGPURendering() {
  private_data_->state_ = DisplayPlanePrivateState::State::kScanout;
}

void DisplayPlaneState::SetOverlayLayer(const OverlayLayer *layer) {
  private_data_->layer_ = layer;
}

void DisplayPlaneState::ReUseOffScreenTarget() {
  if (surface_swapped_) {
    ETRACE(
        "Surface has been swapped and being re-used as offscreen target. \n");
  }
  recycled_surface_ = true;
}

bool DisplayPlaneState::SurfaceRecycled() const {
  return recycled_surface_;
}

const OverlayLayer *DisplayPlaneState::GetOverlayLayer() const {
  return private_data_->layer_;
}

void DisplayPlaneState::SetOffScreenTarget(NativeSurface *target) {
  HwcRect<float> scaled_rect = HwcRect<float>(private_data_->display_frame_);
  if (private_data_->down_scaling_factor_ > 1) {
    scaled_rect.left /= private_data_->down_scaling_factor_;
    scaled_rect.top /= private_data_->down_scaling_factor_;
    scaled_rect.right /= private_data_->down_scaling_factor_;
    scaled_rect.bottom /= private_data_->down_scaling_factor_;
  }

  private_data_->layer_ = target->GetLayer();
  target->ResetDisplayFrame(private_data_->display_frame_);
  if (private_data_->use_plane_scalar_) {
    ETRACE("use plane scaler");
    target->ResetSourceCrop(private_data_->source_crop_);
    target->SetDamageDownScalingFactor(1);
  } else {
    ETRACE("use scaler plane");
    ETRACE("scaler scaled_rect:%f %f %f %f, scaling_factor:%d\n", scaled_rect.left, scaled_rect.right, scaled_rect.top, scaled_rect.bottom, private_data_->down_scaling_factor_);
    target->ResetSourceCrop(scaled_rect);
    if(private_data_->down_scaling_factor_ > 1)
       target->SetDamageDownScalingFactor(private_data_->down_scaling_factor_);
    else
       target->SetDamageDownScalingFactor(1);
  }
  private_data_->surfaces_.emplace(private_data_->surfaces_.begin(), target);
  recycled_surface_ = false;
  refresh_needed_ = true;
  if (private_data_->surfaces_.size() == 1) {
    refresh_needed_ = false;
  }

  surface_swapped_ = true;
}

NativeSurface *DisplayPlaneState::GetOffScreenTarget() const {
  if (private_data_->surfaces_.size() == 0) {
    return NULL;
  }

  return private_data_->surfaces_.at(0);
}

void DisplayPlaneState::SwapSurfaceIfNeeded() {
  if (surface_swapped_) {
    if (recycled_surface_) {
      ETRACE(
          "Surface has been swapped and being re-used as offscreen target. \n");
    }
    return;
  }

  size_t size = private_data_->surfaces_.size();

  if (size == 3) {
    std::vector<NativeSurface *> temp;
    temp.reserve(size);
    // Lets make sure front buffer is now back in the list.
    temp.emplace_back(private_data_->surfaces_.at(1));
    temp.emplace_back(private_data_->surfaces_.at(2));
    temp.emplace_back(private_data_->surfaces_.at(0));
    private_data_->surfaces_.swap(temp);
  }

  surface_swapped_ = true;
  recycled_surface_ = false;
  NativeSurface *surface = private_data_->surfaces_.at(0);
  surface->SetInUse(true);
  private_data_->layer_ = surface->GetLayer();
}

const std::vector<NativeSurface *> &DisplayPlaneState::GetSurfaces() const {
  return private_data_->surfaces_;
}

void DisplayPlaneState::ReleaseSurfaces() {
  std::vector<NativeSurface *>().swap(private_data_->surfaces_);
}

void DisplayPlaneState::RefreshSurfaces(bool clear_surface) {
  const HwcRect<int> &target_display_frame = private_data_->display_frame_;
  const HwcRect<float> &target_src_rect = private_data_->source_crop_;
    HwcRect<float> scaled_rect = target_display_frame;
    if (private_data_->down_scaling_factor_ > 1) {
      scaled_rect.right = scaled_rect.right/private_data_->down_scaling_factor_;
      scaled_rect.left = scaled_rect.left/private_data_->down_scaling_factor_;
      scaled_rect.top = scaled_rect.top/private_data_->down_scaling_factor_;
      scaled_rect.bottom = scaled_rect.bottom/private_data_->down_scaling_factor_;
    }

  for (NativeSurface *surface : private_data_->surfaces_) {
    surface->ResetDisplayFrame(target_display_frame);
    if (private_data_->use_plane_scalar_) {
      surface->ResetSourceCrop(target_src_rect);
    } else {
      surface->ResetSourceCrop(scaled_rect);
    }

    if (!surface->ClearSurface() && clear_surface) {
      surface->SetClearSurface(NativeSurface::kFullClear);
      if (private_data_->use_plane_scalar_) {
	surface->UpdateSurfaceDamage(target_src_rect, target_src_rect);
      } else {
	surface->UpdateSurfaceDamage(target_display_frame, target_display_frame);
      }
    }

   if(private_data_->down_scaling_factor_ > 1)
     surface->SetDamageDownScalingFactor(private_data_->down_scaling_factor_);
  }

  std::vector<CompositionRegion>().swap(private_data_->composition_region_);
  refresh_needed_ = false;
}

DisplayPlane *DisplayPlaneState::GetDisplayPlane() const {
  return private_data_->plane_;
}

const std::vector<size_t> &DisplayPlaneState::GetSourceLayers() const {
  return private_data_->source_layers_;
}

std::vector<CompositionRegion> &DisplayPlaneState::GetCompositionRegion() {
  return private_data_->composition_region_;
}

void DisplayPlaneState::ResetCompositionRegion() {
  if (!private_data_->composition_region_.empty())
    std::vector<CompositionRegion>().swap(private_data_->composition_region_);
}

bool DisplayPlaneState::IsCursorPlane() const {
  return private_data_->type_ == DisplayPlanePrivateState::PlaneType::kCursor;
}

bool DisplayPlaneState::HasCursorLayer() const {
  return private_data_->has_cursor_layer_;
}

bool DisplayPlaneState::IsVideoPlane() const {
  return private_data_->type_ == DisplayPlanePrivateState::PlaneType::kVideo;
}

void DisplayPlaneState::SetVideoPlane() {
  private_data_->type_ = DisplayPlanePrivateState::PlaneType::kVideo;
}

void DisplayPlaneState::UsePlaneScalar(bool enable) {
  if (private_data_->use_plane_scalar_ != enable) {
    private_data_->use_plane_scalar_ = enable;
    RefreshSurfaces(true);
  }
}

bool DisplayPlaneState::IsUsingPlaneScalar() const {
  return private_data_->use_plane_scalar_;
}

void DisplayPlaneState::SetApplyEffects(bool apply_effects) {
  private_data_->apply_effects_ = apply_effects;
  // Doesn't have any impact on planes which
  // are not meant for video purpose.
  if (private_data_->type_ != DisplayPlanePrivateState::PlaneType::kVideo) {
    private_data_->apply_effects_ = false;
  }
}

bool DisplayPlaneState::ApplyEffects() const {
  return private_data_->apply_effects_;
}

bool DisplayPlaneState::Scanout() const {
  if (recycled_surface_) {
    return true;
  }

  if (private_data_->apply_effects_) {
    return false;
  }

  return private_data_->state_ == DisplayPlanePrivateState::State::kScanout;
}

bool DisplayPlaneState::NeedsOffScreenComposition() const {
  if (private_data_->state_ == DisplayPlanePrivateState::State::kRender)
    return true;

  if (recycled_surface_) {
    return true;
  }

  if (private_data_->apply_effects_) {
    return true;
  }

  return false;
}

DisplayPlaneState::ReValidationType DisplayPlaneState::IsRevalidationNeeded()
    const {
  return re_validate_layer_;
}

void DisplayPlaneState::RevalidationDone() {
  re_validate_layer_ = ReValidationType::kNone;
}

bool DisplayPlaneState::CanSquash() const {
  if (private_data_->state_ == DisplayPlanePrivateState::State::kScanout)
    return false;

  if (private_data_->type_ == DisplayPlanePrivateState::PlaneType::kVideo)
    return false;

  return true;
}

void DisplayPlaneState::ValidateReValidation() {
  if (private_data_->source_layers_.size() == 1 &&
      !(private_data_->type_ == DisplayPlanePrivateState::PlaneType::kVideo)) {
    re_validate_layer_ = ReValidationType::kScanout;
  } else {
    bool use_scalar = CanUseDisplayUpScaling();
    if (private_data_->use_plane_scalar_ != use_scalar) {
      re_validate_layer_ = ReValidationType::kScalar;
    } else {
      bool down_scale = CanUseGPUDownScaling();
      if ((private_data_->down_scaling_factor_ > 0) != down_scale) {
        re_validate_layer_ = ReValidationType::kScalar;
      }
    }
  }
}

bool DisplayPlaneState::CanUseDisplayUpScaling() const {
  // TODO: Handle case where all layers to be compoisted have same scaling
  // ratio.

  if (!private_data_->check_display_scalar_) {
    return private_data_->can_use_display_scalar_;
  }

  private_data_->check_display_scalar_ = false;
  bool value = true;

  // We cannot use plane scaling for Layers with different scaling ratio.
  if (private_data_->source_layers_.size() > 1) {
    value = false;
  } else if (private_data_->use_plane_scalar_ &&
             !private_data_->check_downscaling_) {
    value = false;
  }

  if (value) {
    const HwcRect<int> &target_display_frame = private_data_->display_frame_;
    const HwcRect<float> &target_src_rect = private_data_->source_crop_;
    uint32_t display_frame_width =
        target_display_frame.right - target_display_frame.left;
    uint32_t display_frame_height =
        target_display_frame.bottom - target_display_frame.top;
    uint32_t source_crop_width = static_cast<uint32_t>(
        ceilf(target_src_rect.right - target_src_rect.left));
    uint32_t source_crop_height = static_cast<uint32_t>(
        ceilf(target_src_rect.bottom - target_src_rect.top));

    if (value) {
      // Source and Display frame width, height are same and scaling is not
      // needed.
      if ((display_frame_width == source_crop_width) &&
          (display_frame_height == source_crop_height)) {
        value = false;
      }

      if (value) {
        // Display frame width, height is lesser than Source. Let's downscale
        // it with our compositor backend.
        if ((display_frame_width < source_crop_width) &&
            (display_frame_height < source_crop_height)) {
          value = false;
        }
      }

      if (value) {
        // Display frame height is less. If the cost of upscaling width is less
        // than downscaling height, than return.
        if ((display_frame_width > source_crop_width) &&
            (display_frame_height < source_crop_height)) {
          uint32_t width_cost =
              (display_frame_width - source_crop_width) * display_frame_height;
          uint32_t height_cost =
              (source_crop_height - display_frame_height) * display_frame_width;
          if (height_cost > width_cost) {
            value = false;
          }
        }
      }

      if (value) {
        // Display frame width is less. If the cost of upscaling height is less
        // than downscaling width, than return.
        if ((display_frame_width < source_crop_width) &&
            (display_frame_height > source_crop_height)) {
          uint32_t width_cost =
              (source_crop_width - display_frame_width) * display_frame_height;
          uint32_t height_cost =
              (display_frame_height - source_crop_height) * display_frame_width;
          if (width_cost > height_cost) {
            value = false;
          }
        }
      }
    }
  }

  private_data_->can_use_display_scalar_ = value;

  return private_data_->can_use_display_scalar_;
}

bool DisplayPlaneState::CanUseGPUDownScaling() const {
  if (!private_data_->check_downscaling_) {
    ETRACE("Cached %d \n", private_data_->can_use_downscaling_);
    return private_data_->can_use_downscaling_;
  }

  bool value = false;
  private_data_->check_downscaling_ = false;
  private_data_->can_use_downscaling_ = false;

  if (!NeedsOffScreenComposition()) {
    value = false;
    ETRACE("failed1 %d \n", value);
  } else if (private_data_->use_plane_scalar_ &&
             !private_data_->check_display_scalar_) {
    value = false;
    ETRACE("failed2 %d \n", value);
  } else {
    const HwcRect<int> &target_display_frame = private_data_->display_frame_;
    const HwcRect<float> &target_src_rect = private_data_->source_crop_;

    uint32_t display_frame_width =
        target_display_frame.right - target_display_frame.left;
    uint32_t display_frame_height =
        target_display_frame.bottom - target_display_frame.top;
    uint32_t source_crop_width = static_cast<uint32_t>(
        ceilf(target_src_rect.right - target_src_rect.left));
    uint32_t source_crop_height = static_cast<uint32_t>(
        ceilf(target_src_rect.bottom - target_src_rect.top));
    ETRACE("check %d %d %d %d %d \n", display_frame_width, display_frame_height,
           source_crop_width, source_crop_height,
           (source_crop_width - (source_crop_width / 4)));
    if (display_frame_width < 500) {
      // Ignore < 500 pixels.
      value = false;
    } else if ((display_frame_width == source_crop_width) &&
               (display_frame_height == source_crop_height)) {
      value = true;
      ETRACE("passed1 %d \n", value);
    } else {
      // If we are already downscaling content by less than 25%, no need
      // for any further downscaling.
      if (display_frame_width > (source_crop_width - (source_crop_width / 4))) {
        value = true;
        ETRACE("passed2 %d \n", value);
      }
    }
  }

  private_data_->can_use_downscaling_ = value;

  return private_data_->can_use_display_scalar_;
}

void DisplayPlaneState::RefreshSurfacesIfNeeded() {
  if (refresh_needed_) {
    RefreshSurfaces(true);
  } else {
    // Let's make sure we reset our composition region cache
    // of all planes in case we are adding some indexes.
    ResetCompositionRegion();
  }
}

void DisplayPlaneState::SetDisplayDownScalingFactor(uint32_t factor,
                                                    bool update_surfaces) {
  if (private_data_->down_scaling_factor_ == factor)
    return;

  private_data_->down_scaling_factor_ = factor;

  if (update_surfaces)
    RefreshSurfaces(false);
}

uint32_t DisplayPlaneState::GetDownScalingFactor() const {
  return private_data_->down_scaling_factor_;
}

}  // namespace hwcomposer
