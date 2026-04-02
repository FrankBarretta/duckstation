// SPDX-FileCopyrightText: 2019-2026 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#include "d3d9_device.h"

#include "d3d_common.h"

#include "common/error.h"
#include "common/log.h"

#include <algorithm>
#include <cstring>

LOG_CHANNEL(GPUDevice);

namespace {

static constexpr u32 D3D9_SHADER_MODEL = 30;

static D3DTEXTUREADDRESS GetAddressMode(GPUSampler::AddressMode mode)
{
  switch (mode)
  {
    case GPUSampler::AddressMode::Repeat:
      return D3DTADDRESS_WRAP;

    case GPUSampler::AddressMode::ClampToEdge:
      return D3DTADDRESS_CLAMP;

    case GPUSampler::AddressMode::ClampToBorder:
      return D3DTADDRESS_BORDER;

    case GPUSampler::AddressMode::MirrorRepeat:
      return D3DTADDRESS_MIRROR;

    default:
      return D3DTADDRESS_CLAMP;
  }
}

static D3DTEXTUREFILTERTYPE GetFilter(GPUSampler::Filter filter, bool anisotropic)
{
  if (anisotropic)
    return D3DTEXF_ANISOTROPIC;

  return (filter == GPUSampler::Filter::Nearest) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
}

static std::optional<D3DFORMAT> GetD3D9Format(GPUTextureFormat format)
{
  switch (format)
  {
    case GPUTextureFormat::RGBA8:
    case GPUTextureFormat::BGRA8:
      return D3DFMT_A8R8G8B8;

    case GPUTextureFormat::RGB565:
      return D3DFMT_R5G6B5;

    case GPUTextureFormat::RGB5A1:
      return D3DFMT_A1R5G5B5;

    case GPUTextureFormat::R8:
      return D3DFMT_L8;

    case GPUTextureFormat::R16:
    case GPUTextureFormat::R16U:
      return D3DFMT_L16;

    case GPUTextureFormat::D16:
      return D3DFMT_D16;

    case GPUTextureFormat::D24S8:
      return D3DFMT_D24S8;

    default:
      return std::nullopt;
  }
}

static GPUTextureFormat GetGPUTextureFormat(D3DFORMAT format)
{
  switch (format)
  {
    case D3DFMT_A8R8G8B8:
      return GPUTextureFormat::BGRA8;

    case D3DFMT_R5G6B5:
      return GPUTextureFormat::RGB565;

    case D3DFMT_A1R5G5B5:
      return GPUTextureFormat::RGB5A1;

    case D3DFMT_L8:
      return GPUTextureFormat::R8;

    case D3DFMT_L16:
      return GPUTextureFormat::R16;

    case D3DFMT_D16:
      return GPUTextureFormat::D16;

    case D3DFMT_D24S8:
      return GPUTextureFormat::D24S8;

    default:
      return GPUTextureFormat::Unknown;
  }
}

} // namespace

D3D9Sampler::D3D9Sampler(const Config& config) : m_config(config)
{
}

D3D9Sampler::~D3D9Sampler() = default;

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9Sampler::SetDebugName(std::string_view name)
{
}

#endif

D3D9Texture::D3D9Texture(u32 width, u32 height, u32 layers, u32 levels, u32 samples, Type type,
                         GPUTextureFormat format, Flags flags, ComPtr<IDirect3DBaseTexture9> texture,
                         ComPtr<IDirect3DSurface9> surface)
  : GPUTexture(static_cast<u16>(width), static_cast<u16>(height), static_cast<u8>(layers), static_cast<u8>(levels),
               static_cast<u8>(samples), type, format, flags),
    m_texture(std::move(texture)), m_surface(std::move(surface))
{
}

D3D9Texture::~D3D9Texture() = default;

std::unique_ptr<D3D9Texture> D3D9Texture::Create(IDirect3DDevice9* device, u32 width, u32 height, u32 layers,
                                                 u32 levels, u32 samples, Type type, GPUTextureFormat format,
                                                 Flags flags, const void* initial_data, u32 initial_data_stride,
                                                 Error* error)
{
  if (!GPUTexture::ValidateConfig(width, height, layers, levels, samples, type, format, flags, error))
    return {};

  if (layers != 1)
  {
    Error::SetStringView(error, "Direct3D 9 does not support texture arrays in this backend.");
    return {};
  }
  if (samples != 1)
  {
    Error::SetStringView(error, "Direct3D 9 multisampled textures are not implemented yet.");
    return {};
  }
  if (levels != 1)
  {
    Error::SetStringView(error, "Direct3D 9 mipmapped textures are not implemented yet.");
    return {};
  }

  const std::optional<D3DFORMAT> d3d_format = GetD3D9Format(format);
  if (!d3d_format.has_value())
  {
    Error::SetStringFmt(error, "Unsupported Direct3D 9 texture format {}.", GPUTexture::GetFormatName(format));
    return {};
  }

  ComPtr<IDirect3DBaseTexture9> texture;
  ComPtr<IDirect3DSurface9> surface;
  HRESULT hr = E_FAIL;

  if (type == Type::DepthStencil)
  {
    ComPtr<IDirect3DSurface9> ds;
    hr = device->CreateDepthStencilSurface(width, height, d3d_format.value(), D3DMULTISAMPLE_NONE, 0, TRUE,
                                           ds.GetAddressOf(), nullptr);
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreateDepthStencilSurface() failed: ", hr);
      return {};
    }

    surface = std::move(ds);
  }
  else
  {
    const DWORD usage = (type == Type::RenderTarget) ? D3DUSAGE_RENDERTARGET : 0;
    const D3DPOOL pool = (type == Type::RenderTarget) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

    ComPtr<IDirect3DTexture9> tex;
    hr = device->CreateTexture(width, height, levels, usage, d3d_format.value(), pool, tex.GetAddressOf(), nullptr);
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreateTexture() failed: ", hr);
      return {};
    }

    hr = tex->GetSurfaceLevel(0, surface.GetAddressOf());
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DTexture9::GetSurfaceLevel() failed: ", hr);
      return {};
    }

    texture = tex;
  }

  std::unique_ptr<D3D9Texture> ret =
    std::unique_ptr<D3D9Texture>(new D3D9Texture(width, height, layers, levels, samples, type, format, flags,
                                                 std::move(texture), std::move(surface)));

  if (initial_data && !ret->Update(0, 0, width, height, initial_data, initial_data_stride))
  {
    Error::SetStringView(error, "Failed to upload initial Direct3D 9 texture data.");
    return {};
  }

  return ret;
}

bool D3D9Texture::Update(u32 x, u32 y, u32 width, u32 height, const void* data, u32 pitch, u32 layer, u32 level)
{
  if (!m_texture || layer != 0 || level != 0)
    return false;

  IDirect3DTexture9* const texture = static_cast<IDirect3DTexture9*>(m_texture.Get());
  RECT rc = {static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
  D3DLOCKED_RECT lock_rect;
  const HRESULT hr = texture->LockRect(level, &lock_rect, &rc, 0);
  if (FAILED(hr))
    return false;

  GPUTexture::CopyTextureDataForUpload(width, height, m_format, lock_rect.pBits, static_cast<u32>(lock_rect.Pitch),
                                       data, pitch);
  texture->UnlockRect(level);
  return true;
}

bool D3D9Texture::Map(void** map, u32* map_stride, u32 x, u32 y, u32 width, u32 height, u32 layer, u32 level)
{
  if (!m_texture || m_is_mapped || layer != 0 || level != 0)
    return false;

  IDirect3DTexture9* const texture = static_cast<IDirect3DTexture9*>(m_texture.Get());
  RECT rc = {static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
  const HRESULT hr = texture->LockRect(level, &m_locked_rect, &rc, 0);
  if (FAILED(hr))
    return false;

  m_is_mapped = true;
  *map = m_locked_rect.pBits;
  *map_stride = static_cast<u32>(m_locked_rect.Pitch);
  return true;
}

void D3D9Texture::Unmap()
{
  if (!m_texture || !m_is_mapped)
    return;

  static_cast<IDirect3DTexture9*>(m_texture.Get())->UnlockRect(0);
  m_locked_rect = {};
  m_is_mapped = false;
}

void D3D9Texture::GenerateMipmaps()
{
}

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9Texture::SetDebugName(std::string_view name)
{
}

#endif

D3D9DownloadTexture::D3D9DownloadTexture(ComPtr<IDirect3DDevice9> device, ComPtr<IDirect3DSurface9> surface,
                                         u32 width, u32 height, GPUTextureFormat format, void* imported_memory,
                                         u32 imported_stride)
  : GPUDownloadTexture(width, height, format, imported_memory != nullptr), m_device(std::move(device)),
    m_surface(std::move(surface)), m_imported_memory(imported_memory), m_imported_stride(imported_stride)
{
  if (m_imported_memory)
  {
    m_map_pointer = static_cast<const u8*>(m_imported_memory);
    m_current_pitch = m_imported_stride;
  }
}

D3D9DownloadTexture::~D3D9DownloadTexture()
{
  if (m_is_mapped)
    Unmap();
}

std::unique_ptr<D3D9DownloadTexture> D3D9DownloadTexture::Create(IDirect3DDevice9* device, u32 width, u32 height,
                                                                 GPUTextureFormat format, Error* error)
{
  const std::optional<D3DFORMAT> d3d_format = GetD3D9Format(format);
  if (!d3d_format.has_value())
  {
    Error::SetStringFmt(error, "Unsupported Direct3D 9 download texture format {}.",
                        GPUTexture::GetFormatName(format));
    return {};
  }

  ComPtr<IDirect3DSurface9> surface;
  const HRESULT hr = device->CreateOffscreenPlainSurface(width, height, d3d_format.value(), D3DPOOL_SYSTEMMEM,
                                                         surface.GetAddressOf(), nullptr);
  if (FAILED(hr))
  {
    Error::SetHResult(error, "IDirect3DDevice9::CreateOffscreenPlainSurface() failed: ", hr);
    return {};
  }

  ComPtr<IDirect3DDevice9> device_ref(device);
  return std::unique_ptr<D3D9DownloadTexture>(
    new D3D9DownloadTexture(std::move(device_ref), std::move(surface), width, height, format, nullptr, 0));
}

std::unique_ptr<D3D9DownloadTexture> D3D9DownloadTexture::Create(IDirect3DDevice9* device, u32 width, u32 height,
                                                                 GPUTextureFormat format, void* memory,
                                                                 size_t memory_size, u32 memory_stride,
                                                                 Error* error)
{
  const u32 required_size = memory_stride * height;
  if (!memory || memory_size < required_size)
  {
    Error::SetStringFmt(error, "Imported download texture buffer is too small (need {} bytes, got {}).",
                        required_size, memory_size);
    return {};
  }

  std::unique_ptr<D3D9DownloadTexture> texture = Create(device, width, height, format, error);
  if (!texture)
    return {};

  texture->m_is_imported = true;
  texture->m_imported_memory = memory;
  texture->m_imported_stride = memory_stride;
  texture->m_map_pointer = static_cast<const u8*>(memory);
  texture->m_current_pitch = memory_stride;
  return texture;
}

bool D3D9DownloadTexture::CopySurfaceRegionToSurface(IDirect3DSurface9* src_surface, u32 src_x, u32 src_y, u32 width,
                                                     u32 height, u32 dst_x, u32 dst_y)
{
  D3DSURFACE_DESC src_desc = {};
  if (FAILED(src_surface->GetDesc(&src_desc)))
    return false;

  if (dst_x == 0 && dst_y == 0 && width == m_width && height == m_height && src_x == 0 && src_y == 0 &&
      src_desc.Width == m_width && src_desc.Height == m_height)
  {
    return SUCCEEDED(m_device->GetRenderTargetData(src_surface, m_surface.Get()));
  }

  ComPtr<IDirect3DSurface9> temp_rt;
  HRESULT hr = m_device->CreateRenderTarget(width, height, src_desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                            temp_rt.GetAddressOf(), nullptr);
  if (FAILED(hr))
    return false;

  RECT src_rect = {static_cast<LONG>(src_x), static_cast<LONG>(src_y), static_cast<LONG>(src_x + width),
                   static_cast<LONG>(src_y + height)};
  hr = m_device->StretchRect(src_surface, &src_rect, temp_rt.Get(), nullptr, D3DTEXF_NONE);
  if (FAILED(hr))
    return false;

  ComPtr<IDirect3DSurface9> temp_sys;
  hr = m_device->CreateOffscreenPlainSurface(width, height, src_desc.Format, D3DPOOL_SYSTEMMEM, temp_sys.GetAddressOf(),
                                             nullptr);
  if (FAILED(hr))
    return false;

  hr = m_device->GetRenderTargetData(temp_rt.Get(), temp_sys.Get());
  if (FAILED(hr))
    return false;

  D3DLOCKED_RECT src_lock = {};
  D3DLOCKED_RECT dst_lock = {};
  hr = temp_sys->LockRect(&src_lock, nullptr, D3DLOCK_READONLY);
  if (FAILED(hr))
    return false;

  hr = m_surface->LockRect(&dst_lock, nullptr, 0);
  if (FAILED(hr))
  {
    temp_sys->UnlockRect();
    return false;
  }

  const u32 row_size = width * GPUTexture::GetPixelSize(m_format);
  for (u32 row = 0; row < height; row++)
  {
    const u8* src_ptr = static_cast<const u8*>(src_lock.pBits) + (row * static_cast<u32>(src_lock.Pitch));
    u8* dst_ptr = static_cast<u8*>(dst_lock.pBits) + ((dst_y + row) * static_cast<u32>(dst_lock.Pitch)) +
                  (dst_x * GPUTexture::GetPixelSize(m_format));
    std::memcpy(dst_ptr, src_ptr, row_size);
  }

  m_surface->UnlockRect();
  temp_sys->UnlockRect();
  return true;
}

bool D3D9DownloadTexture::CopySurfaceToImportedMemory(u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  if (!m_imported_memory)
    return true;

  D3DLOCKED_RECT lock = {};
  if (FAILED(m_surface->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
    return false;

  const u32 bpp = GPUTexture::GetPixelSize(m_format);
  const u32 row_size = width * bpp;
  for (u32 row = 0; row < height; row++)
  {
    const u8* src_ptr = static_cast<const u8*>(lock.pBits) + ((dst_y + row) * static_cast<u32>(lock.Pitch)) +
                        (dst_x * bpp);
    u8* dst_ptr = static_cast<u8*>(m_imported_memory) + ((dst_y + row) * m_imported_stride) + (dst_x * bpp);
    std::memcpy(dst_ptr, src_ptr, row_size);
  }

  m_surface->UnlockRect();
  return true;
}

void D3D9DownloadTexture::CopyFromTexture(u32 dst_x, u32 dst_y, GPUTexture* src, u32 src_x, u32 src_y, u32 width,
                                          u32 height, u32 src_layer, u32 src_level, bool use_transfer_pitch)
{
  D3D9Texture* src9 = static_cast<D3D9Texture*>(src);
  if (src9->GetFormat() != m_format || src_layer != 0 || src_level != 0 ||
      (src_x + width) > src9->GetWidth() || (src_y + height) > src9->GetHeight() ||
      (dst_x + width) > m_width || (dst_y + height) > m_height ||
      ((dst_x != 0 || dst_y != 0) && use_transfer_pitch))
  {
    ERROR_LOG("D3D9DownloadTexture::CopyFromTexture() received invalid arguments.");
    return;
  }

  if (m_is_mapped && !m_imported_memory)
    Unmap();

  GPUDevice::GetStatistics().num_downloads++;

  if (!CopySurfaceRegionToSurface(src9->GetD3DSurface(), src_x, src_y, width, height, dst_x, dst_y))
  {
    ERROR_LOG("D3D9DownloadTexture::CopyFromTexture() failed.");
    return;
  }

  if (!CopySurfaceToImportedMemory(dst_x, dst_y, width, height))
    ERROR_LOG("D3D9DownloadTexture::CopyFromTexture() failed to update imported memory.");

  m_needs_flush = false;
}

bool D3D9DownloadTexture::Map(u32 x, u32 y, u32 width, u32 height)
{
  if (m_imported_memory)
  {
    m_map_pointer = static_cast<const u8*>(m_imported_memory);
    m_current_pitch = m_imported_stride;
    return true;
  }

  if (m_is_mapped)
    return true;

  const HRESULT hr = m_surface->LockRect(&m_locked_rect, nullptr, D3DLOCK_READONLY);
  if (FAILED(hr))
  {
    ERROR_LOG("D3D9DownloadTexture::Map() failed: {:08X}", static_cast<u32>(hr));
    return false;
  }

  m_is_mapped = true;
  m_map_pointer = static_cast<const u8*>(m_locked_rect.pBits);
  m_current_pitch = static_cast<u32>(m_locked_rect.Pitch);
  return true;
}

void D3D9DownloadTexture::Unmap()
{
  if (m_imported_memory)
    return;

  if (!m_is_mapped)
    return;

  m_surface->UnlockRect();
  m_locked_rect = {};
  m_is_mapped = false;
  m_map_pointer = nullptr;
  m_current_pitch = 0;
}

void D3D9DownloadTexture::Flush()
{
  m_needs_flush = false;
}

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9DownloadTexture::SetDebugName(std::string_view name)
{
}

#endif

D3D9Shader::D3D9Shader(GPUShaderStage stage, ComPtr<IUnknown> shader, std::vector<u8> bytecode)
  : GPUShader(stage), m_shader(std::move(shader)), m_bytecode(std::move(bytecode))
{
}

D3D9Shader::~D3D9Shader() = default;

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9Shader::SetDebugName(std::string_view name)
{
}

#endif

struct D3D9ScreenQuadVertex
{
  float x, y;
  float u, v;
};

D3D9Pipeline::D3D9Pipeline(ComPtr<IDirect3DVertexDeclaration9> vertex_decl, ComPtr<IDirect3DVertexShader9> vertex_shader,
                           ComPtr<IDirect3DPixelShader9> pixel_shader, D3DPRIMITIVETYPE primitive,
                           const RasterizationState& rasterization, const DepthState& depth, const BlendState& blend,
                           Layout layout, u32 vertex_stride, bool uses_internal_screen_quad)
  : m_vertex_decl(std::move(vertex_decl)), m_vertex_shader(std::move(vertex_shader)),
    m_pixel_shader(std::move(pixel_shader)), m_primitive(primitive), m_rasterization(rasterization), m_depth(depth),
    m_blend(blend), m_layout(layout), m_vertex_stride(vertex_stride),
    m_uses_internal_screen_quad(uses_internal_screen_quad)
{
}

D3D9Pipeline::~D3D9Pipeline() = default;

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9Pipeline::SetDebugName(std::string_view name)
{
}

#endif

D3D9SwapChain::D3D9SwapChain(const WindowInfo& wi, GPUVSyncMode vsync_mode) : GPUSwapChain(wi, vsync_mode)
{
}

D3D9SwapChain::~D3D9SwapChain() = default;

bool D3D9SwapChain::ResizeBuffers(u32 new_width, u32 new_height, Error* error)
{
  m_window_info.surface_width = static_cast<u16>(new_width);
  m_window_info.surface_height = static_cast<u16>(new_height);
  return true;
}

bool D3D9SwapChain::SetVSyncMode(GPUVSyncMode mode, Error* error)
{
  m_vsync_mode = mode;
  return true;
}

D3D9Device::D3D9Device()
{
  m_render_api = RenderAPI::D3D9;
}

D3D9Device::~D3D9Device() = default;

void D3D9Device::SetUnimplementedError(Error* error, std::string_view what)
{
  Error::SetStringFmt(error, "Native Direct3D 9 backend: {} not implemented yet.", what);
}

UINT D3D9Device::GetPresentInterval(GPUVSyncMode mode)
{
  switch (mode)
  {
    case GPUVSyncMode::FIFO:
      return D3DPRESENT_INTERVAL_ONE;

    case GPUVSyncMode::Mailbox:
    case GPUVSyncMode::Disabled:
    default:
      return D3DPRESENT_INTERVAL_IMMEDIATE;
  }
}

void D3D9Device::SetFeatures()
{
  D3DCAPS9 caps = {};
  if (m_d3d && SUCCEEDED(m_d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps)))
  {
    m_max_texture_size = std::min<u32>(caps.MaxTextureWidth, caps.MaxTextureHeight);
    m_max_render_targets = std::max<u32>(caps.NumSimultaneousRTs, 1u);
  }
  else
  {
    m_max_texture_size = 4096;
    m_max_render_targets = 1;
  }

  m_render_api_version = 900;
  m_max_multisamples = 1;
  m_features.dual_source_blend = false;
  m_features.framebuffer_fetch = false;
  m_features.per_sample_shading = false;
  m_features.noperspective_interpolation = false;
  m_features.texture_copy_to_self = false;
  m_features.texture_buffers = false;
  m_features.texture_buffers_emulated_with_ssbo = false;
  m_features.feedback_loops = false;
  m_features.geometry_shaders = false;
  m_features.compute_shaders = false;
  m_features.partial_msaa_resolve = false;
  m_features.memory_import = false;
  m_features.exclusive_fullscreen = false;
  m_features.explicit_present = false;
  m_features.timed_present = false;
  m_features.gpu_timing = false;
  m_features.shader_cache = false;
  m_features.pipeline_cache = false;
  m_features.prefer_unused_textures = false;
  m_features.raster_order_views = false;
  m_features.dxt_textures = true;
  m_features.bptc_textures = false;
}

std::string D3D9Device::GetDriverInfo() const
{
  if (!m_d3d)
    return "Direct3D 9";

  D3DADAPTER_IDENTIFIER9 ident = {};
  if (FAILED(m_d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident)))
    return "Direct3D 9";

  return std::string(ident.Description) + "\nDriver: " + ident.Driver;
}

void D3D9Device::FlushCommands()
{
}

void D3D9Device::WaitForGPUIdle()
{
  if (m_device)
    m_device->EndScene();
}

std::unique_ptr<GPUSwapChain> D3D9Device::CreateSwapChain(const WindowInfo& wi, GPUVSyncMode vsync_mode,
                                                          const ExclusiveFullscreenMode* exclusive_fullscreen_mode,
                                                          std::optional<bool> exclusive_fullscreen_control,
                                                          Error* error)
{
  if (wi.type != WindowInfoType::Win32)
  {
    Error::SetStringView(error, "Cannot create a Direct3D 9 swap chain on non-Win32 window.");
    return {};
  }

  return std::make_unique<D3D9SwapChain>(wi, vsync_mode);
}

std::unique_ptr<GPUTexture> D3D9Device::CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                      GPUTexture::Type type, GPUTextureFormat format,
                                                      GPUTexture::Flags flags, const void* data, u32 data_stride,
                                                      Error* error)
{
  return D3D9Texture::Create(m_device.Get(), width, height, layers, levels, samples, type, format, flags, data,
                             data_stride, error);
}

std::unique_ptr<GPUSampler> D3D9Device::CreateSampler(const GPUSampler::Config& config, Error* error)
{
  return std::make_unique<D3D9Sampler>(config);
}

std::unique_ptr<GPUTextureBuffer> D3D9Device::CreateTextureBuffer(GPUTextureBuffer::Format format,
                                                                  u32 size_in_elements, Error* error)
{
  SetUnimplementedError(error, "CreateTextureBuffer");
  return {};
}

std::unique_ptr<GPUDownloadTexture> D3D9Device::CreateDownloadTexture(u32 width, u32 height,
                                                                      GPUTextureFormat format, Error* error)
{
  return D3D9DownloadTexture::Create(m_device.Get(), width, height, format, error);
}

std::unique_ptr<GPUDownloadTexture> D3D9Device::CreateDownloadTexture(u32 width, u32 height,
                                                                      GPUTextureFormat format, void* memory,
                                                                      size_t memory_size, u32 memory_stride,
                                                                      Error* error)
{
  return D3D9DownloadTexture::Create(m_device.Get(), width, height, format, memory, memory_size, memory_stride,
                                     error);
}

void D3D9Device::CopyTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level,
                                   GPUTexture* src, u32 src_x, u32 src_y, u32 src_layer, u32 src_level, u32 width,
                                   u32 height)
{
  if (!m_device)
    return;

  D3D9Texture* const dst9 = static_cast<D3D9Texture*>(dst);
  D3D9Texture* const src9 = static_cast<D3D9Texture*>(src);
  if (!dst9 || !src9 || dst_layer != 0 || dst_level != 0 || src_layer != 0 || src_level != 0 ||
      dst9->GetFormat() != src9->GetFormat())
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() received unsupported parameters.");
    return;
  }

  const RECT src_rect = {static_cast<LONG>(src_x), static_cast<LONG>(src_y), static_cast<LONG>(src_x + width),
                         static_cast<LONG>(src_y + height)};
  const RECT dst_rect = {static_cast<LONG>(dst_x), static_cast<LONG>(dst_y), static_cast<LONG>(dst_x + width),
                         static_cast<LONG>(dst_y + height)};
  const u32 row_size = width * GPUTexture::GetPixelSize(src9->GetFormat());

  const std::optional<D3DFORMAT> d3d_format = GetD3D9Format(src9->GetFormat());
  if (!d3d_format.has_value())
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() unsupported format {}.", GPUTexture::GetFormatName(src9->GetFormat()));
    return;
  }

  GPUDevice::GetStatistics().num_copies++;

  ComPtr<IDirect3DSurface9> temp_sys;
  HRESULT hr = m_device->CreateOffscreenPlainSurface(width, height, d3d_format.value(), D3DPOOL_SYSTEMMEM,
                                                     temp_sys.GetAddressOf(), nullptr);
  if (FAILED(hr))
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() CreateOffscreenPlainSurface failed: {:08X}", static_cast<u32>(hr));
    return;
  }

  if (src9->IsDepthStencil() || dst9->IsDepthStencil())
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() depth-stencil copies are not supported.");
    return;
  }

  if (src9->IsRenderTarget())
  {
    ComPtr<IDirect3DSurface9> copy_source = src9->GetD3DSurface();
    ComPtr<IDirect3DSurface9> temp_rt;
    const bool needs_intermediate_rt =
      (src_x != 0 || src_y != 0 || width != src9->GetWidth() || height != src9->GetHeight());
    if (needs_intermediate_rt)
    {
      hr = m_device->CreateRenderTarget(width, height, d3d_format.value(), D3DMULTISAMPLE_NONE, 0, FALSE,
                                        temp_rt.GetAddressOf(), nullptr);
      if (FAILED(hr))
      {
        ERROR_LOG("D3D9Device::CopyTextureRegion() CreateRenderTarget failed: {:08X}", static_cast<u32>(hr));
        return;
      }

      hr = m_device->StretchRect(src9->GetD3DSurface(), &src_rect, temp_rt.Get(), nullptr, D3DTEXF_NONE);
      if (FAILED(hr))
      {
        ERROR_LOG("D3D9Device::CopyTextureRegion() StretchRect failed: {:08X}", static_cast<u32>(hr));
        return;
      }

      copy_source = temp_rt;
    }

    hr = m_device->GetRenderTargetData(copy_source.Get(), temp_sys.Get());
    if (FAILED(hr))
    {
      ERROR_LOG("D3D9Device::CopyTextureRegion() GetRenderTargetData failed: {:08X}", static_cast<u32>(hr));
      return;
    }
  }
  else
  {
    IDirect3DTexture9* const src_tex = static_cast<IDirect3DTexture9*>(src9->GetD3DTexture());
    if (!src_tex)
    {
      ERROR_LOG("D3D9Device::CopyTextureRegion() missing source texture handle.");
      return;
    }

    D3DLOCKED_RECT src_lock = {};
    D3DLOCKED_RECT temp_lock = {};
    hr = src_tex->LockRect(0, &src_lock, &src_rect, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
      ERROR_LOG("D3D9Device::CopyTextureRegion() src LockRect failed: {:08X}", static_cast<u32>(hr));
      return;
    }

    hr = temp_sys->LockRect(&temp_lock, nullptr, 0);
    if (FAILED(hr))
    {
      src_tex->UnlockRect(0);
      ERROR_LOG("D3D9Device::CopyTextureRegion() temp LockRect failed: {:08X}", static_cast<u32>(hr));
      return;
    }

    for (u32 row = 0; row < height; row++)
    {
      const u8* src_ptr = static_cast<const u8*>(src_lock.pBits) + (row * static_cast<u32>(src_lock.Pitch));
      u8* dst_ptr = static_cast<u8*>(temp_lock.pBits) + (row * static_cast<u32>(temp_lock.Pitch));
      std::memcpy(dst_ptr, src_ptr, row_size);
    }

    temp_sys->UnlockRect();
    src_tex->UnlockRect(0);
  }

  if (dst9->IsRenderTarget())
  {
    const POINT dst_point = {static_cast<LONG>(dst_x), static_cast<LONG>(dst_y)};
    hr = m_device->UpdateSurface(temp_sys.Get(), nullptr, dst9->GetD3DSurface(), &dst_point);
    if (FAILED(hr))
      ERROR_LOG("D3D9Device::CopyTextureRegion() UpdateSurface failed: {:08X}", static_cast<u32>(hr));
    return;
  }

  IDirect3DTexture9* const dst_tex = static_cast<IDirect3DTexture9*>(dst9->GetD3DTexture());
  if (!dst_tex)
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() missing destination texture handle.");
    return;
  }

  D3DLOCKED_RECT temp_lock = {};
  D3DLOCKED_RECT dst_lock = {};
  hr = temp_sys->LockRect(&temp_lock, nullptr, D3DLOCK_READONLY);
  if (FAILED(hr))
  {
    ERROR_LOG("D3D9Device::CopyTextureRegion() temp read LockRect failed: {:08X}", static_cast<u32>(hr));
    return;
  }

  hr = dst_tex->LockRect(0, &dst_lock, &dst_rect, 0);
  if (FAILED(hr))
  {
    temp_sys->UnlockRect();
    ERROR_LOG("D3D9Device::CopyTextureRegion() dst LockRect failed: {:08X}", static_cast<u32>(hr));
    return;
  }

  for (u32 row = 0; row < height; row++)
  {
    const u8* src_ptr = static_cast<const u8*>(temp_lock.pBits) + (row * static_cast<u32>(temp_lock.Pitch));
    u8* dst_ptr = static_cast<u8*>(dst_lock.pBits) + (row * static_cast<u32>(dst_lock.Pitch));
    std::memcpy(dst_ptr, src_ptr, row_size);
  }

  dst_tex->UnlockRect(0);
  temp_sys->UnlockRect();
}

void D3D9Device::ResolveTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level,
                                      GPUTexture* src, u32 src_x, u32 src_y, u32 width, u32 height)
{
  ERROR_LOG("D3D9Device::ResolveTextureRegion() not implemented.");
}

std::unique_ptr<GPUShader> D3D9Device::CreateShaderFromBinary(GPUShaderStage stage, std::span<const u8> data,
                                                              Error* error)
{
  if (!m_device)
  {
    Error::SetStringView(error, "Direct3D 9 device has not been created.");
    return {};
  }
  if ((data.size() % sizeof(DWORD)) != 0)
  {
    Error::SetStringView(error, "Direct3D 9 shader bytecode size is invalid.");
    return {};
  }

  const DWORD* const bytecode = reinterpret_cast<const DWORD*>(data.data());
  if (stage == GPUShaderStage::Vertex)
  {
    ComPtr<IDirect3DVertexShader9> shader;
    const HRESULT hr = m_device->CreateVertexShader(bytecode, shader.GetAddressOf());
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreateVertexShader() failed: ", hr);
      return {};
    }

    return std::make_unique<D3D9Shader>(stage, shader, std::vector<u8>(data.begin(), data.end()));
  }
  if (stage == GPUShaderStage::Fragment)
  {
    ComPtr<IDirect3DPixelShader9> shader;
    const HRESULT hr = m_device->CreatePixelShader(bytecode, shader.GetAddressOf());
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreatePixelShader() failed: ", hr);
      return {};
    }

    return std::make_unique<D3D9Shader>(stage, shader, std::vector<u8>(data.begin(), data.end()));
  }

  Error::SetStringView(error, "Direct3D 9 backend only supports vertex and fragment shaders.");
  return {};
}

std::unique_ptr<GPUShader> D3D9Device::CreateShaderFromSource(GPUShaderStage stage, GPUShaderLanguage language,
                                                              std::string_view source, const char* entry_point,
                                                              DynamicHeapArray<u8>* out_binary, Error* error)
{
  if (language != GPUShaderLanguage::HLSL)
  {
    Error::SetStringView(error, "Direct3D 9 backend only supports HLSL shaders.");
    return {};
  }

  std::optional<DynamicHeapArray<u8>> bytecode =
    D3DCommon::CompileShader(D3D9_SHADER_MODEL, m_debug_device, stage, source, entry_point, error);
  if (!bytecode.has_value())
    return {};

  if (out_binary)
    *out_binary = bytecode.value();

  return CreateShaderFromBinary(stage, bytecode->cspan(), error);
}

std::unique_ptr<GPUPipeline> D3D9Device::CreatePipeline(const GPUPipeline::GraphicsConfig& config, Error* error)
{
  if (!m_device)
  {
    Error::SetStringView(error, "Direct3D 9 device has not been created.");
    return {};
  }
  if (config.geometry_shader)
  {
    Error::SetStringView(error, "Direct3D 9 backend does not support geometry shaders.");
    return {};
  }

  const D3D9Shader* const vs = static_cast<const D3D9Shader*>(config.vertex_shader);
  const D3D9Shader* const ps = static_cast<const D3D9Shader*>(config.fragment_shader);
  if (!vs || !ps)
  {
    Error::SetStringView(error, "Direct3D 9 graphics pipelines require vertex and fragment shaders.");
    return {};
  }

  static constexpr std::array<D3DPRIMITIVETYPE, static_cast<u32>(GPUPipeline::Primitive::MaxCount)> primitive_mapping = {{
    D3DPT_POINTLIST,
    D3DPT_LINELIST,
    D3DPT_TRIANGLELIST,
    D3DPT_TRIANGLESTRIP,
  }};

  static constexpr std::array<D3DDECLUSAGE, static_cast<u32>(GPUPipeline::VertexAttribute::Semantic::MaxCount)> semantic_mapping = {{
    D3DDECLUSAGE_POSITION,
    D3DDECLUSAGE_TEXCOORD,
    D3DDECLUSAGE_COLOR,
  }};

  static constexpr D3DDECLTYPE format_mapping[static_cast<u8>(GPUPipeline::VertexAttribute::Type::MaxCount)][4] = {
    {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4},
    {D3DDECLTYPE_UBYTE4, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UBYTE4},
    {D3DDECLTYPE_UBYTE4N, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UBYTE4N},
    {D3DDECLTYPE_UBYTE4N, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UNUSED, D3DDECLTYPE_UBYTE4N},
    {D3DDECLTYPE_SHORT2, D3DDECLTYPE_SHORT4, D3DDECLTYPE_UNUSED, D3DDECLTYPE_SHORT4},
    {D3DDECLTYPE_SHORT2, D3DDECLTYPE_SHORT4, D3DDECLTYPE_UNUSED, D3DDECLTYPE_SHORT4},
    {D3DDECLTYPE_SHORT2N, D3DDECLTYPE_SHORT4N, D3DDECLTYPE_UNUSED, D3DDECLTYPE_SHORT4N},
    {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4},
    {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4},
  };

  std::vector<D3DVERTEXELEMENT9> elems;
  const bool uses_internal_screen_quad = config.input_layout.vertex_attributes.empty();
  if (uses_internal_screen_quad)
  {
    elems = {
      D3DVERTEXELEMENT9{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
      D3DVERTEXELEMENT9{0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    };
  }
  else
  {
    elems.reserve(config.input_layout.vertex_attributes.size() + 1);
    for (const GPUPipeline::VertexAttribute attr : config.input_layout.vertex_attributes)
    {
      const u8 components = attr.components;
      const u8 type_index = static_cast<u8>(attr.type.GetValue());
      if (components == 0 || components > 4)
      {
        Error::SetStringView(error, "Unsupported Direct3D 9 vertex attribute component count.");
        return {};
      }

      const D3DDECLTYPE decl_type = format_mapping[type_index][components - 1];
      if (decl_type == D3DDECLTYPE_UNUSED)
      {
        Error::SetStringView(error, "Unsupported Direct3D 9 vertex attribute format.");
        return {};
      }

      elems.push_back(D3DVERTEXELEMENT9{0, static_cast<WORD>(attr.offset.GetValue()), static_cast<BYTE>(decl_type),
                                        static_cast<BYTE>(D3DDECLMETHOD_DEFAULT),
                                        static_cast<BYTE>(semantic_mapping[static_cast<u8>(attr.semantic.GetValue())]),
                                        static_cast<BYTE>(attr.semantic_index.GetValue())});
    }
  }
  elems.push_back(D3DDECL_END());

  ComPtr<IDirect3DVertexDeclaration9> vertex_decl;
  HRESULT hr = m_device->CreateVertexDeclaration(elems.data(), vertex_decl.GetAddressOf());
  if (FAILED(hr))
  {
    Error::SetHResult(error, "IDirect3DDevice9::CreateVertexDeclaration() failed: ", hr);
    return {};
  }

  return std::make_unique<D3D9Pipeline>(std::move(vertex_decl), vs->GetVertexShader(), ps->GetPixelShader(),
                                        primitive_mapping[static_cast<u8>(config.primitive)], config.rasterization,
                                        config.depth, config.blend, config.layout,
                                        uses_internal_screen_quad ? sizeof(D3D9ScreenQuadVertex) :
                                                                    config.input_layout.vertex_stride,
                                        uses_internal_screen_quad);
}

std::unique_ptr<GPUPipeline> D3D9Device::CreatePipeline(const GPUPipeline::ComputeConfig& config, Error* error)
{
  SetUnimplementedError(error, "CreatePipeline(Compute)");
  return {};
}

#ifdef ENABLE_GPU_OBJECT_NAMES

void D3D9Device::PushDebugGroup(const char* name)
{
}

void D3D9Device::PopDebugGroup()
{
}

void D3D9Device::InsertDebugMessage(const char* msg)
{
}

#endif

void D3D9Device::MapVertexBuffer(u32 vertex_size, u32 vertex_count, void** map_ptr, u32* map_space,
                                 u32* map_base_vertex)
{
  if (!EnsureVertexBufferSpace(vertex_size, vertex_count))
  {
    *map_ptr = nullptr;
    *map_space = 0;
    *map_base_vertex = 0;
    return;
  }

  const u32 required_size = vertex_size * vertex_count;
  void* ptr = nullptr;
  const DWORD flags = (m_vertex_buffer_position == 0) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
  if (FAILED(m_vertex_buffer->Lock(m_vertex_buffer_position, required_size, &ptr, flags)))
  {
    *map_ptr = nullptr;
    *map_space = 0;
    *map_base_vertex = 0;
    return;
  }

  m_current_base_vertex = m_vertex_buffer_position / vertex_size;
  *map_ptr = ptr;
  *map_space = vertex_count;
  *map_base_vertex = m_current_base_vertex;
}

void D3D9Device::UnmapVertexBuffer(u32 vertex_size, u32 vertex_count)
{
  if (!m_vertex_buffer)
    return;

  m_vertex_buffer->Unlock();
  m_vertex_buffer_position += vertex_size * vertex_count;
}

void D3D9Device::MapIndexBuffer(u32 index_count, DrawIndex** map_ptr, u32* map_space, u32* map_base_index)
{
  if (!EnsureIndexBufferSpace(index_count))
  {
    *map_ptr = nullptr;
    *map_space = 0;
    *map_base_index = 0;
    return;
  }

  void* ptr = nullptr;
  const u32 required_size = index_count * sizeof(DrawIndex);
  const DWORD flags = (m_index_buffer_position == 0) ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
  if (FAILED(m_index_buffer->Lock(m_index_buffer_position, required_size, &ptr, flags)))
  {
    *map_ptr = nullptr;
    *map_space = 0;
    *map_base_index = 0;
    return;
  }

  m_current_base_index = m_index_buffer_position / sizeof(DrawIndex);
  *map_ptr = static_cast<DrawIndex*>(ptr);
  *map_space = index_count;
  *map_base_index = m_current_base_index;
}

void D3D9Device::UnmapIndexBuffer(u32 used_size)
{
  if (!m_index_buffer)
    return;

  m_index_buffer->Unlock();
  m_index_buffer_position += used_size * sizeof(DrawIndex);
}

void* D3D9Device::MapUniformBuffer(u32 size)
{
  m_uniform_buffer_data.resize(size);
  return m_uniform_buffer_data.data();
}

void D3D9Device::UnmapUniformBuffer(u32 size)
{
}

void D3D9Device::SetRenderTargets(GPUTexture* const* rts, u32 num_rts, GPUTexture* ds,
                                  GPUPipeline::RenderPassFlag flags)
{
  if (!m_device)
    return;

  const u32 clamped_num_rts = std::min(num_rts, m_max_render_targets);
  if (clamped_num_rts != num_rts)
    WARNING_LOG("D3D9 backend clamped {} render targets to device limit {}.", num_rts, clamped_num_rts);

  for (u32 slot = 0; slot < MAX_TEXTURE_SAMPLERS; slot++)
  {
    D3D9Texture* const current_texture = m_current_textures[slot];
    if (!current_texture)
      continue;

    bool conflicts = (ds != nullptr && current_texture == static_cast<D3D9Texture*>(ds));
    for (u32 rt_index = 0; !conflicts && rt_index < clamped_num_rts; rt_index++)
      conflicts = (rts[rt_index] != nullptr && current_texture == static_cast<D3D9Texture*>(rts[rt_index]));

    if (conflicts)
    {
      m_current_textures[slot] = nullptr;
      m_device->SetTexture(slot, nullptr);
    }
  }

  IDirect3DSurface9* rt_surface0 = m_backbuffer.Get();
  if (clamped_num_rts > 0 && rts && rts[0])
    rt_surface0 = static_cast<D3D9Texture*>(rts[0])->GetD3DSurface();

  m_device->SetRenderTarget(0, rt_surface0);
  for (u32 i = 1; i < clamped_num_rts; i++)
  {
    IDirect3DSurface9* const rt_surface = (rts && rts[i]) ? static_cast<D3D9Texture*>(rts[i])->GetD3DSurface() : nullptr;
    m_device->SetRenderTarget(i, rt_surface);
  }
  for (u32 i = clamped_num_rts; i < m_num_current_render_targets; i++)
    m_device->SetRenderTarget(i, nullptr);

  m_num_current_render_targets = clamped_num_rts;
  m_device->SetDepthStencilSurface(ds ? static_cast<D3D9Texture*>(ds)->GetD3DSurface() : nullptr);
}

void D3D9Device::SetPipeline(GPUPipeline* pipeline)
{
  m_current_pipeline = static_cast<D3D9Pipeline*>(pipeline);
  ApplyPipelineState(m_current_pipeline);
}

void D3D9Device::SetTextureSampler(u32 slot, GPUTexture* texture, GPUSampler* sampler)
{
  if (!m_device)
    return;

  D3D9Texture* const d3d_texture = static_cast<D3D9Texture*>(texture ? texture : m_empty_texture.get());
  D3D9Sampler* const d3d_sampler = static_cast<D3D9Sampler*>(sampler ? sampler : m_linear_sampler);
  const GPUSampler::Config config = d3d_sampler ? d3d_sampler->GetConfig() : GPUSampler::GetLinearConfig();
  const bool anisotropic = (config.anisotropy.GetValue() > 1);

  m_current_textures[slot] = d3d_texture;
  m_device->SetTexture(slot, d3d_texture ? d3d_texture->GetD3DTexture() : nullptr);
  m_device->SetSamplerState(slot, D3DSAMP_ADDRESSU, GetAddressMode(config.address_u));
  m_device->SetSamplerState(slot, D3DSAMP_ADDRESSV, GetAddressMode(config.address_v));
  m_device->SetSamplerState(slot, D3DSAMP_ADDRESSW, GetAddressMode(config.address_w));
  m_device->SetSamplerState(slot, D3DSAMP_MINFILTER, GetFilter(config.min_filter, anisotropic));
  m_device->SetSamplerState(slot, D3DSAMP_MAGFILTER, GetFilter(config.mag_filter, anisotropic));
  m_device->SetSamplerState(slot, D3DSAMP_MIPFILTER, GetFilter(config.mip_filter, anisotropic));
  m_device->SetSamplerState(slot, D3DSAMP_MAXANISOTROPY, std::max<u32>(config.anisotropy, 1u));
  m_device->SetSamplerState(slot, D3DSAMP_BORDERCOLOR, config.border_color);
}

void D3D9Device::SetTextureBuffer(u32 slot, GPUTextureBuffer* buffer)
{
}

void D3D9Device::SetViewport(const GSVector4i rc)
{
  if (!m_device)
    return;

  D3DVIEWPORT9 vp = {};
  vp.X = rc.left;
  vp.Y = rc.top;
  vp.Width = rc.width();
  vp.Height = rc.height();
  vp.MinZ = 0.0f;
  vp.MaxZ = 1.0f;
  m_device->SetViewport(&vp);
}

void D3D9Device::SetScissor(const GSVector4i rc)
{
  if (m_device)
    m_device->SetScissorRect(reinterpret_cast<const RECT*>(&rc));
}

void D3D9Device::Draw(u32 vertex_count, u32 base_vertex)
{
  if (!m_device || !m_current_pipeline)
    return;

  ApplyUniforms(nullptr, 0);
  m_device->SetStreamSource(0,
                            m_current_pipeline->UsesInternalScreenQuad() ? m_screen_quad_vertex_buffer.Get() :
                                                                          m_vertex_buffer.Get(),
                            0, m_current_pipeline->GetVertexStride());
  const UINT primitive_count = (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLESTRIP) ? (vertex_count - 2) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLELIST) ? (vertex_count / 3) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_LINELIST) ? (vertex_count / 2) : vertex_count;
  m_device->DrawPrimitive(m_current_pipeline->GetPrimitive(), base_vertex, primitive_count);
}

void D3D9Device::DrawWithPushConstants(u32 vertex_count, u32 base_vertex, const void* push_constants,
                                       u32 push_constants_size)
{
  if (!m_device || !m_current_pipeline)
    return;

  ApplyUniforms(push_constants, push_constants_size);
  m_device->SetStreamSource(0,
                            m_current_pipeline->UsesInternalScreenQuad() ? m_screen_quad_vertex_buffer.Get() :
                                                                          m_vertex_buffer.Get(),
                            0, m_current_pipeline->GetVertexStride());
  const UINT primitive_count = (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLESTRIP) ? (vertex_count - 2) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLELIST) ? (vertex_count / 3) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_LINELIST) ? (vertex_count / 2) : vertex_count;
  m_device->DrawPrimitive(m_current_pipeline->GetPrimitive(), base_vertex, primitive_count);
}

void D3D9Device::DrawIndexed(u32 index_count, u32 base_index, u32 base_vertex)
{
  if (!m_device || !m_current_pipeline)
    return;

  ApplyUniforms(nullptr, 0);
  m_device->SetStreamSource(0,
                            m_current_pipeline->UsesInternalScreenQuad() ? m_screen_quad_vertex_buffer.Get() :
                                                                          m_vertex_buffer.Get(),
                            0, m_current_pipeline->GetVertexStride());
  m_device->SetIndices(m_index_buffer.Get());
  const UINT primitive_count = (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLESTRIP) ? (index_count - 2) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLELIST) ? (index_count / 3) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_LINELIST) ? (index_count / 2) : index_count;
  const u32 total_vertices = m_current_pipeline->UsesInternalScreenQuad() ? 3u :
                              (m_current_pipeline->GetVertexStride() > 0 ?
                                 (m_vertex_buffer_position / m_current_pipeline->GetVertexStride()) :
                                 0u);
  const u32 available_vertices = (base_vertex < total_vertices) ? (total_vertices - base_vertex) : 0u;
  m_device->DrawIndexedPrimitive(m_current_pipeline->GetPrimitive(), static_cast<INT>(base_vertex), 0,
                                 static_cast<UINT>(available_vertices), base_index, primitive_count);
}

void D3D9Device::DrawIndexedWithPushConstants(u32 index_count, u32 base_index, u32 base_vertex,
                                              const void* push_constants, u32 push_constants_size)
{
  if (!m_device || !m_current_pipeline)
    return;

  ApplyUniforms(push_constants, push_constants_size);
  m_device->SetStreamSource(0,
                            m_current_pipeline->UsesInternalScreenQuad() ? m_screen_quad_vertex_buffer.Get() :
                                                                          m_vertex_buffer.Get(),
                            0, m_current_pipeline->GetVertexStride());
  m_device->SetIndices(m_index_buffer.Get());
  const UINT primitive_count = (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLESTRIP) ? (index_count - 2) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_TRIANGLELIST) ? (index_count / 3) :
                               (m_current_pipeline->GetPrimitive() == D3DPT_LINELIST) ? (index_count / 2) : index_count;
  const u32 total_vertices = m_current_pipeline->UsesInternalScreenQuad() ? 3u :
                              (m_current_pipeline->GetVertexStride() > 0 ?
                                 (m_vertex_buffer_position / m_current_pipeline->GetVertexStride()) :
                                 0u);
  const u32 available_vertices = (base_vertex < total_vertices) ? (total_vertices - base_vertex) : 0u;
  m_device->DrawIndexedPrimitive(m_current_pipeline->GetPrimitive(), static_cast<INT>(base_vertex), 0,
                                 static_cast<UINT>(available_vertices), base_index, primitive_count);
}

void D3D9Device::Dispatch(u32 threads_x, u32 threads_y, u32 threads_z, u32 group_size_x, u32 group_size_y,
                          u32 group_size_z)
{
}

void D3D9Device::DispatchWithPushConstants(u32 threads_x, u32 threads_y, u32 threads_z, u32 group_size_x,
                                           u32 group_size_y, u32 group_size_z, const void* push_constants,
                                           u32 push_constants_size)
{
}

GPUPresentResult D3D9Device::BeginPresent(GPUSwapChain* swap_chain, u32 clear_color)
{
  if (!m_device)
    return GPUPresentResult::DeviceLost;

  m_vertex_buffer_position = 0;
  m_index_buffer_position = 0;
  m_current_pipeline = nullptr;

  m_device->SetRenderTarget(0, m_backbuffer.Get());
  m_device->SetDepthStencilSurface(nullptr);
  m_device->BeginScene();

  const auto color = RGBA8ToFloat(clear_color);
  m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(color[0], color[1], color[2], color[3]), 1.0f, 0);
  return GPUPresentResult::OK;
}

void D3D9Device::EndPresent(GPUSwapChain* swap_chain, bool explicit_present, u64 present_time)
{
  if (!m_device)
    return;

  m_device->EndScene();

  const HRESULT hr = m_device->Present(nullptr, nullptr, nullptr, nullptr);
  if (FAILED(hr))
    ERROR_LOG("IDirect3DDevice9::Present() failed: {:08X}", static_cast<unsigned>(hr));
}

void D3D9Device::SubmitPresent(GPUSwapChain* swap_chain)
{
}

bool D3D9Device::SupportsTextureFormat(GPUTextureFormat format) const
{
  switch (format)
  {
    case GPUTextureFormat::RGBA8:
    case GPUTextureFormat::BGRA8:
    case GPUTextureFormat::RGB565:
    case GPUTextureFormat::RGB5A1:
    case GPUTextureFormat::R8:
    case GPUTextureFormat::R16U:
    case GPUTextureFormat::D16:
    case GPUTextureFormat::D24S8:
      return true;

    default:
      return false;
  }
}

bool D3D9Device::CreateDeviceAndMainSwapChain(std::string_view adapter, CreateFlags create_flags, const WindowInfo& wi,
                                              GPUVSyncMode vsync_mode,
                                              const ExclusiveFullscreenMode* exclusive_fullscreen_mode,
                                              std::optional<bool> exclusive_fullscreen_control, Error* error)
{
  if (wi.type != WindowInfoType::Win32)
  {
    Error::SetStringView(error, "Direct3D 9 backend currently supports Win32 windows only.");
    return false;
  }

  m_d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
  if (!m_d3d)
  {
    Error::SetStringView(error, "Direct3DCreate9() failed.");
    return false;
  }

  std::memset(&m_present_params, 0, sizeof(m_present_params));
  m_present_params.Windowed = TRUE;
  m_present_params.hDeviceWindow = reinterpret_cast<HWND>(wi.window_handle);
  m_present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  m_present_params.BackBufferFormat = D3DFMT_A8R8G8B8;
  m_present_params.BackBufferWidth = wi.surface_width;
  m_present_params.BackBufferHeight = wi.surface_height;
  m_present_params.PresentationInterval = GetPresentInterval(vsync_mode);
  m_present_params.EnableAutoDepthStencil = FALSE;

  DWORD create_flags_hw = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED;
  HRESULT hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, reinterpret_cast<HWND>(wi.window_handle),
                                   create_flags_hw, &m_present_params, m_device.ReleaseAndGetAddressOf());
  if (FAILED(hr))
  {
    const DWORD create_flags_sw = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE |
                                  D3DCREATE_MULTITHREADED;
    hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, reinterpret_cast<HWND>(wi.window_handle),
                             create_flags_sw, &m_present_params, m_device.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3D9::CreateDevice() failed: ", hr);
      return false;
    }
  }

  SetFeatures();
  hr = m_device->GetRenderTarget(0, m_backbuffer.ReleaseAndGetAddressOf());
  if (FAILED(hr))
  {
    Error::SetHResult(error, "IDirect3DDevice9::GetRenderTarget() failed: ", hr);
    return false;
  }

  D3DSURFACE_DESC backbuffer_desc = {};
  hr = m_backbuffer->GetDesc(&backbuffer_desc);
  if (FAILED(hr))
  {
    Error::SetHResult(error, "IDirect3DSurface9::GetDesc() failed: ", hr);
    return false;
  }

  // Use an oversized triangle (3 vertices) that covers the full viewport when rendered as a triangle list.
  // This matches what other APIs do with SV_VertexID-generated vertices in the fullscreen quad vertex shader.
  // The viewport/scissor clips the triangle to the visible area, and UVs interpolate correctly.
  hr = m_device->CreateVertexBuffer(sizeof(D3D9ScreenQuadVertex) * 3, 0, 0, D3DPOOL_MANAGED,
                                    m_screen_quad_vertex_buffer.ReleaseAndGetAddressOf(), nullptr);
  if (FAILED(hr))
  {
    Error::SetHResult(error, "IDirect3DDevice9::CreateVertexBuffer(screen quad) failed: ", hr);
    return false;
  }

  if (void* quad_vertices; SUCCEEDED(m_screen_quad_vertex_buffer->Lock(0, 0, &quad_vertices, 0)))
  {
    static constexpr std::array<D3D9ScreenQuadVertex, 3> verts = {{
      {-1.0f, 1.0f, 0.0f, 0.0f},
      {3.0f, 1.0f, 2.0f, 0.0f},
      {-1.0f, -3.0f, 0.0f, 2.0f},
    }};
    std::memcpy(quad_vertices, verts.data(), sizeof(verts));
    m_screen_quad_vertex_buffer->Unlock();
  }

  WindowInfo swap_chain_wi(wi);
  swap_chain_wi.surface_width = static_cast<u16>(backbuffer_desc.Width);
  swap_chain_wi.surface_height = static_cast<u16>(backbuffer_desc.Height);
  swap_chain_wi.surface_format = GetGPUTextureFormat(backbuffer_desc.Format);
  if (swap_chain_wi.surface_format == GPUTextureFormat::Unknown)
  {
    WARNING_LOG("Unknown D3D9 backbuffer format {}, falling back to BGRA8 for swap-chain metadata.",
                static_cast<u32>(backbuffer_desc.Format));
    swap_chain_wi.surface_format = GPUTextureFormat::BGRA8;
  }

  VERBOSE_LOG("D3D9 swap chain buffer size: {}x{}, format {}", swap_chain_wi.surface_width,
              swap_chain_wi.surface_height, GPUTexture::GetFormatName(swap_chain_wi.surface_format));

  m_main_swap_chain = std::make_unique<D3D9SwapChain>(swap_chain_wi, vsync_mode);
  return true;
}

void D3D9Device::DestroyDevice()
{
  m_current_pipeline = nullptr;
  m_uniform_buffer_data.clear();
  m_num_current_render_targets = 0;
  m_backbuffer.Reset();
  m_vertex_buffer.Reset();
  m_index_buffer.Reset();
  m_screen_quad_vertex_buffer.Reset();
  m_main_swap_chain.reset();
  m_device.Reset();
  m_d3d.Reset();
}

bool D3D9Device::EnsureVertexBufferSpace(u32 vertex_size, u32 vertex_count, Error* error)
{
  const u32 required_size = std::max<u32>(vertex_size * vertex_count, 64 * 1024);
  if (!m_vertex_buffer || required_size > m_vertex_buffer_size)
  {
    m_vertex_buffer.Reset();
    const HRESULT hr = m_device->CreateVertexBuffer(required_size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                    D3DPOOL_DEFAULT, m_vertex_buffer.GetAddressOf(), nullptr);
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreateVertexBuffer() failed: ", hr);
      return false;
    }

    m_vertex_buffer_size = required_size;
    m_vertex_buffer_position = 0;
  }
  else if ((m_vertex_buffer_position + required_size) > m_vertex_buffer_size)
  {
    m_vertex_buffer_position = 0;
  }

  return true;
}

bool D3D9Device::EnsureIndexBufferSpace(u32 index_count, Error* error)
{
  const u32 required_size = std::max<u32>(index_count * sizeof(DrawIndex), 16 * 1024);
  if (!m_index_buffer || required_size > m_index_buffer_size)
  {
    m_index_buffer.Reset();
    const HRESULT hr = m_device->CreateIndexBuffer(required_size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                                   D3DFMT_INDEX16, D3DPOOL_DEFAULT, m_index_buffer.GetAddressOf(),
                                                   nullptr);
    if (FAILED(hr))
    {
      Error::SetHResult(error, "IDirect3DDevice9::CreateIndexBuffer() failed: ", hr);
      return false;
    }

    m_index_buffer_size = required_size;
    m_index_buffer_position = 0;
  }
  else if ((m_index_buffer_position + required_size) > m_index_buffer_size)
  {
    m_index_buffer_position = 0;
  }

  return true;
}

u32 D3D9Device::GetConstantRegisterCount(u32 size)
{
  return (size + 15) / 16;
}

void D3D9Device::ApplyUniforms(const void* push_constants, u32 push_constants_size)
{
  if (!m_device)
    return;

  if (!m_uniform_buffer_data.empty())
  {
    m_device->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(m_uniform_buffer_data.data()),
                                       GetConstantRegisterCount(static_cast<u32>(m_uniform_buffer_data.size())));
    m_device->SetPixelShaderConstantF(0, reinterpret_cast<const float*>(m_uniform_buffer_data.data()),
                                      GetConstantRegisterCount(static_cast<u32>(m_uniform_buffer_data.size())));
  }

  if (push_constants && push_constants_size > 0)
  {
    m_device->SetVertexShaderConstantF(32, reinterpret_cast<const float*>(push_constants),
                                       GetConstantRegisterCount(push_constants_size));
    m_device->SetPixelShaderConstantF(32, reinterpret_cast<const float*>(push_constants),
                                      GetConstantRegisterCount(push_constants_size));
  }
}

void D3D9Device::ApplyPipelineState(D3D9Pipeline* pipeline)
{
  if (!m_device || !pipeline)
    return;

  static constexpr std::array<D3DCULL, static_cast<u32>(GPUPipeline::CullMode::MaxCount)> cull_mapping = {{
    D3DCULL_NONE,
    D3DCULL_CW,
    D3DCULL_CCW,
  }};
  static constexpr std::array<D3DCMPFUNC, static_cast<u32>(GPUPipeline::DepthFunc::MaxCount)> depth_mapping = {{
    D3DCMP_NEVER,
    D3DCMP_ALWAYS,
    D3DCMP_LESS,
    D3DCMP_LESSEQUAL,
    D3DCMP_GREATER,
    D3DCMP_GREATEREQUAL,
    D3DCMP_EQUAL,
  }};
  static constexpr std::array<D3DBLEND, static_cast<u32>(GPUPipeline::BlendFunc::MaxCount)> blend_mapping = {{
    D3DBLEND_ZERO,
    D3DBLEND_ONE,
    D3DBLEND_SRCCOLOR,
    D3DBLEND_INVSRCCOLOR,
    D3DBLEND_DESTCOLOR,
    D3DBLEND_INVDESTCOLOR,
    D3DBLEND_SRCALPHA,
    D3DBLEND_INVSRCALPHA,
    D3DBLEND_SRCALPHASAT,
    D3DBLEND_INVSRCALPHA,
    D3DBLEND_DESTALPHA,
    D3DBLEND_INVDESTALPHA,
    D3DBLEND_BLENDFACTOR,
    D3DBLEND_INVBLENDFACTOR,
  }};
  static constexpr std::array<D3DBLENDOP, static_cast<u32>(GPUPipeline::BlendOp::MaxCount)> blend_op_mapping = {{
    D3DBLENDOP_ADD,
    D3DBLENDOP_SUBTRACT,
    D3DBLENDOP_REVSUBTRACT,
    D3DBLENDOP_MIN,
    D3DBLENDOP_MAX,
  }};

  m_device->SetVertexDeclaration(pipeline->GetVertexDeclaration());
  m_device->SetVertexShader(pipeline->GetVertexShader());
  m_device->SetPixelShader(pipeline->GetPixelShader());
  m_device->SetRenderState(D3DRS_CULLMODE,
                           cull_mapping[static_cast<u8>(pipeline->GetRasterizationState().cull_mode.GetValue())]);
  m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
  m_device->SetRenderState(D3DRS_ZENABLE,
                           pipeline->GetDepthState().depth_test != GPUPipeline::DepthFunc::Always ? D3DZB_TRUE : D3DZB_FALSE);
  m_device->SetRenderState(D3DRS_ZWRITEENABLE, pipeline->GetDepthState().depth_write ? TRUE : FALSE);
  m_device->SetRenderState(D3DRS_ZFUNC,
                           depth_mapping[static_cast<u8>(pipeline->GetDepthState().depth_test.GetValue())]);
  m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, pipeline->GetBlendState().enable ? TRUE : FALSE);
  m_device->SetRenderState(D3DRS_SRCBLEND,
                           blend_mapping[static_cast<u8>(pipeline->GetBlendState().src_blend.GetValue())]);
  m_device->SetRenderState(D3DRS_DESTBLEND,
                           blend_mapping[static_cast<u8>(pipeline->GetBlendState().dst_blend.GetValue())]);
  m_device->SetRenderState(D3DRS_BLENDOP,
                           blend_op_mapping[static_cast<u8>(pipeline->GetBlendState().blend_op.GetValue())]);
  m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
  m_device->SetRenderState(D3DRS_SRCBLENDALPHA,
                           blend_mapping[static_cast<u8>(pipeline->GetBlendState().src_alpha_blend.GetValue())]);
  m_device->SetRenderState(D3DRS_DESTBLENDALPHA,
                           blend_mapping[static_cast<u8>(pipeline->GetBlendState().dst_alpha_blend.GetValue())]);
  m_device->SetRenderState(D3DRS_BLENDOPALPHA,
                           blend_op_mapping[static_cast<u8>(pipeline->GetBlendState().alpha_blend_op.GetValue())]);
  m_device->SetRenderState(D3DRS_COLORWRITEENABLE, pipeline->GetBlendState().write_mask);
  m_device->SetRenderState(D3DRS_BLENDFACTOR, pipeline->GetBlendState().constant);
}