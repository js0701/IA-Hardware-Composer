/*
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

#include "gbmbufferhandler.h"

#include <unistd.h>
#include <drm.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

#include <hwcbuffer.h>
#include <hwcdefs.h>
#include <hwctrace.h>
#include <platformdefines.h>

#include "commondrmutils.h"

namespace hwcomposer {

// static
NativeBufferHandler *NativeBufferHandler::CreateInstance(uint32_t fd) {
  GbmBufferHandler *handler = new GbmBufferHandler(fd);
  if (!handler)
    return NULL;

  if (!handler->Init()) {
    ETRACE("Failed to initialize GbmBufferHandler.");
    delete handler;
    return NULL;
  }
  return handler;
}

GbmBufferHandler::GbmBufferHandler(uint32_t fd) : fd_(fd), device_(0) {
}

GbmBufferHandler::~GbmBufferHandler() {
  if (device_)
    gbm_device_destroy(device_);
}

bool GbmBufferHandler::Init() {
  device_ = gbm_create_device(fd_);
  if (!device_) {
    ETRACE("failed to create gbm device \n");
    return false;
  }

  return true;
}

bool GbmBufferHandler::CreateBuffer(uint32_t w, uint32_t h, int format,
                                    HWCNativeHandle *handle,
                                    uint32_t layer_type) {
  uint32_t gbm_format = format;
  if (gbm_format == 0)
    gbm_format = GBM_FORMAT_XRGB8888;

  uint32_t flags = 0;

  if (layer_type == kLayerNormal) {
    flags |= (GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  } else if (layer_type == kLayerVideo) {
    flags |= (GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING /*|
              GBM_BO_USE_CAMERA_WRITE | GBM_BO_USE_CAMERA_READ*/);
  }

  struct gbm_bo *bo = NULL;
  if(gbm_format == DRM_FORMAT_XRGB8888)
  {
    uint64_t modifier = I915_FORMAT_MOD_Y_TILED_CCS;
  	bo = gbm_bo_create_with_modifiers(device_, w, h, gbm_format, &modifier, 1);
  }
  else
  bo = gbm_bo_create(device_, w, h, gbm_format, flags);

 /*
  if (!bo) {
    flags &= ~GBM_BO_USE_SCANOUT;
    bo = gbm_bo_create(device_, w, h, gbm_format, flags);
  }

  if (!bo) {
    flags &= ~GBM_BO_USE_RENDERING;
    bo = gbm_bo_create(device_, w, h, gbm_format, flags);
  }
  */

  if (!bo) {
    ETRACE("GbmBufferHandler: failed to create gbm_bo");
    return false;
  }

  struct gbm_handle *temp = new struct gbm_handle();
  
#if USE_MINIGBM
  temp->import_data.width = gbm_bo_get_width(bo);
  temp->import_data.height = gbm_bo_get_height(bo);
  temp->import_data.format = gbm_bo_get_format(bo);
  size_t total_planes = gbm_bo_get_num_planes(bo);
  for (size_t i = 0; i < total_planes; i++) {
    temp->import_data.fds[i] = gbm_bo_get_plane_fd(bo, i);
    temp->import_data.offsets[i] = gbm_bo_get_plane_offset(bo, i);
    temp->import_data.strides[i] = gbm_bo_get_plane_stride(bo, i);
  }
  temp->total_planes = total_planes;
#else
  if(gbm_format == DRM_FORMAT_XRGB8888) {
  	temp->import_data.fd_modifier_data.width = gbm_bo_get_width(bo);
    temp->import_data.fd_modifier_data.height = gbm_bo_get_height(bo);
    temp->import_data.fd_modifier_data.format = gbm_bo_get_format(bo);
	temp->total_planes = gbm_bo_get_plane_count(bo);//
	temp->import_data.fd_modifier_data.num_fds = 1;
	temp->import_data.fd_modifier_data.fds[0] = gbm_bo_get_fd(bo);
	for(size_t i=0; i< temp->total_planes; i++)
    {  
       temp->import_data.fd_modifier_data.offsets[i] = gbm_bo_get_offset(bo, i);
       temp->import_data.fd_modifier_data.strides[i] = gbm_bo_get_stride_for_plane(bo, i);
    }
	temp->import_data.fd_modifier_data.modifier = gbm_bo_get_modifier(bo);
	temp->modifier = temp->import_data.fd_modifier_data.modifier;
  	
  }
  else {
    temp->import_data.fd_data.width = gbm_bo_get_width(bo);
    temp->import_data.fd_data.height = gbm_bo_get_height(bo);
    temp->import_data.fd_data.format = gbm_bo_get_format(bo);
    temp->import_data.fd_data.fd = gbm_bo_get_fd(bo);
    temp->import_data.fd_data.stride = gbm_bo_get_stride(bo);
    temp->total_planes = gbm_bo_get_plane_count(bo);//drm_bo_get_num_planes(temp->import_data.format);
  }
#endif

  temp->bo = bo;
  temp->hwc_buffer_ = true;
  temp->gbm_flags = flags;
  *handle = temp;

  return true;
}

bool GbmBufferHandler::ReleaseBuffer(HWCNativeHandle handle) {
  if (handle->bo || handle->imported_bo) {
    if (handle->bo && handle->hwc_buffer_) {
      gbm_bo_destroy(handle->bo);
    }

    if (handle->imported_bo) {
      gbm_bo_destroy(handle->imported_bo);
    }
#ifdef USE_MINIGBM
    for (size_t i = 0; i < handle->total_planes; i++)
      close(handle->import_data.fds[i]);
#else
    if(!handle->modifier)
    close(handle->import_data.fd_data.fd);
	else 
	{
	for (size_t i = 0; i < handle->import_data.fd_modifier_data.num_fds; i++)
      close(handle->import_data.fd_modifier_data.fds[i]);
	}
#endif
  }

  return true;
}

void GbmBufferHandler::DestroyHandle(HWCNativeHandle handle) {
  delete handle;
  handle = NULL;
}

void GbmBufferHandler::CopyHandle(HWCNativeHandle source,
                                  HWCNativeHandle *target) {
  struct gbm_handle *temp = new struct gbm_handle();
#if USE_MINIGBM
  temp->import_data.width = source->import_data.width;
  temp->import_data.height = source->import_data.height;
  temp->import_data.format = source->import_data.format;

  size_t total_planes = source->total_planes;
  temp->total_planes  = total_planes;//
  for (size_t i = 0; i < total_planes; i++) {
    temp->import_data.fds[i] = dup(source->import_data.fds[i]);
    temp->import_data.offsets[i] = source->import_data.offsets[i];
    temp->import_data.strides[i] = source->import_data.strides[i];
  }
#else
  if(!source->modifier) {
    temp->import_data.fd_data.width = source->import_data.fd_data.width;
    temp->import_data.fd_data.height = source->import_data.fd_data.height;
    temp->import_data.fd_data.format = source->import_data.fd_data.format;
    temp->import_data.fd_data.fd = dup(source->import_data.fd_data.fd);
    temp->import_data.fd_data.stride = source->import_data.fd_data.stride;
  } else {
    temp->import_data.fd_modifier_data.width =  source->import_data.fd_modifier_data.width;
    temp->import_data.fd_modifier_data.height = source->import_data.fd_modifier_data.height;
    temp->import_data.fd_modifier_data.format = source->import_data.fd_modifier_data.format ;
	temp->total_planes = source->total_planes;//
	temp->import_data.fd_modifier_data.num_fds = source->import_data.fd_modifier_data.num_fds ;
	for(size_t i=0; i< temp->import_data.fd_modifier_data.num_fds; i++)
	  temp->import_data.fd_modifier_data.fds[i] = dup(source->import_data.fd_modifier_data.fds[i]);
	for(size_t i=0; i< temp->total_planes; i++)
    {  
       temp->import_data.fd_modifier_data.offsets[i] = source->import_data.fd_modifier_data.offsets[i];
       temp->import_data.fd_modifier_data.strides[i] = source->import_data.fd_modifier_data.strides[i];
    }
	temp->import_data.fd_modifier_data.modifier = source->import_data.fd_modifier_data.modifier;
	temp->modifier = temp->import_data.fd_modifier_data.modifier;
  
  }
  
#endif

  temp->bo = source->bo;
  temp->total_planes = source->total_planes;
  temp->gbm_flags  = source->gbm_flags;
  *target = temp;
}

bool GbmBufferHandler::ImportBuffer(HWCNativeHandle handle, HwcBuffer *bo) {
  memset(bo, 0, sizeof(struct HwcBuffer));
  uint32_t gem_handle = 0;
  if (!handle->imported_bo) {
#if USE_MINIGBM
     bo->format = handle->import_data.format;
    handle->imported_bo =
        gbm_bo_import(device_, GBM_BO_IMPORT_FD_PLANAR, &handle->import_data,
                      handle->gbm_flags);
     if (!handle->imported_bo) {
        ETRACE("can't import bo");
      }
#else
    if(!handle->modifier)
    {
     bo->format = handle->import_data.fd_data.format;
    handle->imported_bo = gbm_bo_import(
        device_, GBM_BO_IMPORT_FD, &handle->import_data.fd_data, handle->gbm_flags);
    }
	else {
	bo->format = handle->import_data.fd_modifier_data.format;
    handle->imported_bo = gbm_bo_import(
        device_, GBM_BO_IMPORT_FD_MODIFIER, &handle->import_data.fd_modifier_data, handle->gbm_flags);
    }
	
    if (!handle->imported_bo) {
        ETRACE("can't import bo");
    }

#endif
  }

  gem_handle = gbm_bo_get_handle(handle->imported_bo).u32;

  if (!gem_handle) {
    ETRACE("Invalid GEM handle. \n");
    return false;
  }

#if USE_MINIGBM
  bo->width = handle->import_data.width;
  bo->height = handle->import_data.height;
#else
  if(!handle->modifier) {
  	bo->width = handle->import_data.fd_data.width;
    bo->height = handle->import_data.fd_data.height;
  }
  else {
  	bo->width = handle->import_data.fd_modifier_data.width;
    bo->height = handle->import_data.fd_modifier_data.height;
  }   	
#endif

  // FIXME: Set right flag here.
  bo->usage |= hwcomposer::kLayerNormal;

#if USE_MINIGBM
  bo->prime_fd = handle->import_data.fds[0];
  size_t total_planes = gbm_bo_get_num_planes(handle->bo);
  bo->numplanes = total_planes;
  for (size_t i = 0; i < total_planes; i++) {
    bo->gem_handles[i] = gem_handle;
    bo->offsets[i] = gbm_bo_get_plane_offset(handle->bo, i);
    bo->pitches[i] = gbm_bo_get_plane_stride(handle->bo, i);
  }
#else
  if(!handle->modifier) {
  bo->prime_fd = handle->import_data.fd_data.fd;
  bo->modifier = 0;
  }
  else
  {
  bo->prime_fd = handle->import_data.fd_modifier_data.fds[0];
  bo->modifier = gbm_bo_get_modifier(handle->bo);
  }
  
  size_t total_planes = gbm_bo_get_plane_count(handle->imported_bo);
  bo->numplanes = total_planes;
  for(size_t i= 0; i < total_planes; i++ ) {
    bo->gem_handles[i] = gbm_bo_get_handle_for_plane(handle->imported_bo, i).u32;
    bo->offsets[i] = handle->import_data.fd_modifier_data.offsets[i];
    bo->pitches[i] = gbm_bo_get_stride_for_plane(handle->bo, i);
  }
  
#endif

  return true;
}

uint32_t GbmBufferHandler::GetTotalPlanes(HWCNativeHandle handle) {
  return handle->total_planes;
}

void *GbmBufferHandler::Map(HWCNativeHandle handle, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height, uint32_t *stride,
                            void **map_data, size_t plane) {
  if (!handle->bo)
    return NULL;

#if USE_MINIGBM
  return gbm_bo_map(handle->bo, x, y, width, height, GBM_BO_TRANSFER_WRITE,
                    stride, map_data, plane);
#else
  return gbm_bo_map(handle->bo, x, y, width, height, GBM_BO_TRANSFER_WRITE,
                    stride, map_data);
#endif
}

void GbmBufferHandler::UnMap(HWCNativeHandle handle, void *map_data) {
  if (!handle->bo)
    return;

  return gbm_bo_unmap(handle->bo, map_data);
}

}  // namespace hwcomposer
