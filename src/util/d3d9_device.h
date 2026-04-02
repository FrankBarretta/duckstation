// SPDX-FileCopyrightText: 2019-2026 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#pragma once

#include "gpu_device.h"

#include "common/windows_headers.h"

#include <d3d9.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

class D3D9Sampler final : public GPUSampler
{
public:
  explicit D3D9Sampler(const Config& config);
  ~D3D9Sampler() override;

  ALWAYS_INLINE const Config& GetConfig() const { return m_config; }

#ifdef ENABLE_GPU_OBJECT_NAMES
  void SetDebugName(std::string_view name) override;
#endif

private:
  Config m_config;
};

class D3D9Texture final : public GPUTexture
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  ~D3D9Texture() override;

  static std::unique_ptr<D3D9Texture> Create(IDirect3DDevice9* device, u32 width, u32 height, u32 layers, u32 levels,
                                             u32 samples, Type type, GPUTextureFormat format, Flags flags,
                                             const void* initial_data, u32 initial_data_stride, Error* error);

  ALWAYS_INLINE IDirect3DBaseTexture9* GetD3DTexture() const { return m_texture.Get(); }
  ALWAYS_INLINE IDirect3DSurface9* GetD3DSurface() const { return m_surface.Get(); }

  bool Update(u32 x, u32 y, u32 width, u32 height, const void* data, u32 pitch, u32 layer = 0,
              u32 level = 0) override;
  bool Map(void** map, u32* map_stride, u32 x, u32 y, u32 width, u32 height, u32 layer = 0, u32 level = 0) override;
  void Unmap() override;
  void GenerateMipmaps() override;

#ifdef ENABLE_GPU_OBJECT_NAMES
  void SetDebugName(std::string_view name) override;
#endif

private:
  D3D9Texture(u32 width, u32 height, u32 layers, u32 levels, u32 samples, Type type, GPUTextureFormat format,
              Flags flags, ComPtr<IDirect3DBaseTexture9> texture, ComPtr<IDirect3DSurface9> surface);

  ComPtr<IDirect3DBaseTexture9> m_texture;
  ComPtr<IDirect3DSurface9> m_surface;
  D3DLOCKED_RECT m_locked_rect = {};
  bool m_is_mapped = false;
};

class D3D9DownloadTexture final : public GPUDownloadTexture
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  ~D3D9DownloadTexture() override;

  static std::unique_ptr<D3D9DownloadTexture> Create(IDirect3DDevice9* device, u32 width, u32 height,
                                                     GPUTextureFormat format, Error* error);
  static std::unique_ptr<D3D9DownloadTexture> Create(IDirect3DDevice9* device, u32 width, u32 height,
                                                     GPUTextureFormat format, void* memory, size_t memory_size,
                                                     u32 memory_stride, Error* error);

  void CopyFromTexture(u32 dst_x, u32 dst_y, GPUTexture* src, u32 src_x, u32 src_y, u32 width, u32 height,
                       u32 src_layer, u32 src_level, bool use_transfer_pitch) override;
  bool Map(u32 x, u32 y, u32 width, u32 height) override;
  void Unmap() override;
  void Flush() override;

#ifdef ENABLE_GPU_OBJECT_NAMES
  void SetDebugName(std::string_view name) override;
#endif

private:
  D3D9DownloadTexture(ComPtr<IDirect3DDevice9> device, ComPtr<IDirect3DSurface9> surface, u32 width, u32 height,
                      GPUTextureFormat format, void* imported_memory, u32 imported_stride);

  bool CopySurfaceRegionToSurface(IDirect3DSurface9* src_surface, u32 src_x, u32 src_y, u32 width, u32 height,
                                  u32 dst_x, u32 dst_y);
  bool CopySurfaceToImportedMemory(u32 dst_x, u32 dst_y, u32 width, u32 height);

  ComPtr<IDirect3DDevice9> m_device;
  ComPtr<IDirect3DSurface9> m_surface;
  D3DLOCKED_RECT m_locked_rect = {};
  void* m_imported_memory = nullptr;
  u32 m_imported_stride = 0;
  bool m_is_mapped = false;
};

class D3D9Shader final : public GPUShader
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  D3D9Shader(GPUShaderStage stage, ComPtr<IUnknown> shader, std::vector<u8> bytecode);
  ~D3D9Shader() override;

  ALWAYS_INLINE IDirect3DVertexShader9* GetVertexShader() const
  {
    return (m_stage == GPUShaderStage::Vertex) ? static_cast<IDirect3DVertexShader9*>(m_shader.Get()) : nullptr;
  }
  ALWAYS_INLINE IDirect3DPixelShader9* GetPixelShader() const
  {
    return (m_stage == GPUShaderStage::Fragment) ? static_cast<IDirect3DPixelShader9*>(m_shader.Get()) : nullptr;
  }
  ALWAYS_INLINE const std::vector<u8>& GetBytecode() const { return m_bytecode; }

#ifdef ENABLE_GPU_OBJECT_NAMES
  void SetDebugName(std::string_view name) override;
#endif

private:
  ComPtr<IUnknown> m_shader;
  std::vector<u8> m_bytecode;
};

class D3D9Pipeline final : public GPUPipeline
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  D3D9Pipeline(ComPtr<IDirect3DVertexDeclaration9> vertex_decl, ComPtr<IDirect3DVertexShader9> vertex_shader,
               ComPtr<IDirect3DPixelShader9> pixel_shader, D3DPRIMITIVETYPE primitive,
               const RasterizationState& rasterization, const DepthState& depth, const BlendState& blend,
               Layout layout, u32 vertex_stride, bool uses_internal_screen_quad);
  ~D3D9Pipeline() override;

  ALWAYS_INLINE IDirect3DVertexDeclaration9* GetVertexDeclaration() const { return m_vertex_decl.Get(); }
  ALWAYS_INLINE IDirect3DVertexShader9* GetVertexShader() const { return m_vertex_shader.Get(); }
  ALWAYS_INLINE IDirect3DPixelShader9* GetPixelShader() const { return m_pixel_shader.Get(); }
  ALWAYS_INLINE D3DPRIMITIVETYPE GetPrimitive() const { return m_primitive; }
  ALWAYS_INLINE const RasterizationState& GetRasterizationState() const { return m_rasterization; }
  ALWAYS_INLINE const DepthState& GetDepthState() const { return m_depth; }
  ALWAYS_INLINE const BlendState& GetBlendState() const { return m_blend; }
  ALWAYS_INLINE Layout GetLayout() const { return m_layout; }
  ALWAYS_INLINE u32 GetVertexStride() const { return m_vertex_stride; }
  ALWAYS_INLINE bool UsesInternalScreenQuad() const { return m_uses_internal_screen_quad; }

#ifdef ENABLE_GPU_OBJECT_NAMES
  void SetDebugName(std::string_view name) override;
#endif

private:
  ComPtr<IDirect3DVertexDeclaration9> m_vertex_decl;
  ComPtr<IDirect3DVertexShader9> m_vertex_shader;
  ComPtr<IDirect3DPixelShader9> m_pixel_shader;
  D3DPRIMITIVETYPE m_primitive;
  RasterizationState m_rasterization;
  DepthState m_depth;
  BlendState m_blend;
  Layout m_layout;
  u32 m_vertex_stride;
  bool m_uses_internal_screen_quad;
};

class D3D9SwapChain final : public GPUSwapChain
{
public:
  D3D9SwapChain(const WindowInfo& wi, GPUVSyncMode vsync_mode);
  ~D3D9SwapChain() override;

  bool ResizeBuffers(u32 new_width, u32 new_height, Error* error) override;
  bool SetVSyncMode(GPUVSyncMode mode, Error* error) override;
};

class D3D9Device final : public GPUDevice
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  D3D9Device();
  ~D3D9Device() override;

  std::string GetDriverInfo() const override;

  void FlushCommands() override;
  void WaitForGPUIdle() override;

  std::unique_ptr<GPUSwapChain> CreateSwapChain(const WindowInfo& wi, GPUVSyncMode vsync_mode,
                                                const ExclusiveFullscreenMode* exclusive_fullscreen_mode,
                                                std::optional<bool> exclusive_fullscreen_control,
                                                Error* error) override;
  std::unique_ptr<GPUTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                            GPUTexture::Type type, GPUTextureFormat format, GPUTexture::Flags flags,
                                            const void* data = nullptr, u32 data_stride = 0,
                                            Error* error = nullptr) override;
  std::unique_ptr<GPUSampler> CreateSampler(const GPUSampler::Config& config, Error* error = nullptr) override;
  std::unique_ptr<GPUTextureBuffer> CreateTextureBuffer(GPUTextureBuffer::Format format, u32 size_in_elements,
                                                        Error* error = nullptr) override;
  std::unique_ptr<GPUDownloadTexture> CreateDownloadTexture(u32 width, u32 height, GPUTextureFormat format,
                                                            Error* error = nullptr) override;
  std::unique_ptr<GPUDownloadTexture> CreateDownloadTexture(u32 width, u32 height, GPUTextureFormat format,
                                                            void* memory, size_t memory_size, u32 memory_stride,
                                                            Error* error = nullptr) override;
  void CopyTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level, GPUTexture* src,
                         u32 src_x, u32 src_y, u32 src_layer, u32 src_level, u32 width, u32 height) override;
  void ResolveTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level, GPUTexture* src,
                            u32 src_x, u32 src_y, u32 width, u32 height) override;
  std::unique_ptr<GPUShader> CreateShaderFromBinary(GPUShaderStage stage, std::span<const u8> data,
                                                    Error* error) override;
  std::unique_ptr<GPUShader> CreateShaderFromSource(GPUShaderStage stage, GPUShaderLanguage language,
                                                    std::string_view source, const char* entry_point,
                                                    DynamicHeapArray<u8>* out_binary, Error* error) override;
  std::unique_ptr<GPUPipeline> CreatePipeline(const GPUPipeline::GraphicsConfig& config, Error* error) override;
  std::unique_ptr<GPUPipeline> CreatePipeline(const GPUPipeline::ComputeConfig& config, Error* error) override;

#ifdef ENABLE_GPU_OBJECT_NAMES
  void PushDebugGroup(const char* name) override;
  void PopDebugGroup() override;
  void InsertDebugMessage(const char* msg) override;
#endif

  void MapVertexBuffer(u32 vertex_size, u32 vertex_count, void** map_ptr, u32* map_space,
                       u32* map_base_vertex) override;
  void UnmapVertexBuffer(u32 vertex_size, u32 vertex_count) override;
  void MapIndexBuffer(u32 index_count, DrawIndex** map_ptr, u32* map_space, u32* map_base_index) override;
  void UnmapIndexBuffer(u32 used_size) override;
  void* MapUniformBuffer(u32 size) override;
  void UnmapUniformBuffer(u32 size) override;
  void SetRenderTargets(GPUTexture* const* rts, u32 num_rts, GPUTexture* ds,
                        GPUPipeline::RenderPassFlag flags = GPUPipeline::NoRenderPassFlags) override;
  void SetPipeline(GPUPipeline* pipeline) override;
  void SetTextureSampler(u32 slot, GPUTexture* texture, GPUSampler* sampler) override;
  void SetTextureBuffer(u32 slot, GPUTextureBuffer* buffer) override;
  void SetViewport(const GSVector4i rc) override;
  void SetScissor(const GSVector4i rc) override;
  void Draw(u32 vertex_count, u32 base_vertex) override;
  void DrawWithPushConstants(u32 vertex_count, u32 base_vertex, const void* push_constants,
                             u32 push_constants_size) override;
  void DrawIndexed(u32 index_count, u32 base_index, u32 base_vertex) override;
  void DrawIndexedWithPushConstants(u32 index_count, u32 base_index, u32 base_vertex,
                                    const void* push_constants, u32 push_constants_size) override;
  void Dispatch(u32 threads_x, u32 threads_y, u32 threads_z, u32 group_size_x, u32 group_size_y,
                u32 group_size_z) override;
  void DispatchWithPushConstants(u32 threads_x, u32 threads_y, u32 threads_z, u32 group_size_x,
                                 u32 group_size_y, u32 group_size_z, const void* push_constants,
                                 u32 push_constants_size) override;
  GPUPresentResult BeginPresent(GPUSwapChain* swap_chain, u32 clear_color) override;
  void EndPresent(GPUSwapChain* swap_chain, bool explicit_present, u64 present_time) override;
  void SubmitPresent(GPUSwapChain* swap_chain) override;

  bool SupportsTextureFormat(GPUTextureFormat format) const override;

protected:
  bool CreateDeviceAndMainSwapChain(std::string_view adapter, CreateFlags create_flags, const WindowInfo& wi,
                                    GPUVSyncMode vsync_mode, const ExclusiveFullscreenMode* exclusive_fullscreen_mode,
                                    std::optional<bool> exclusive_fullscreen_control, Error* error) override;
  void DestroyDevice() override;

private:
  static UINT GetPresentInterval(GPUVSyncMode mode);
  static void SetUnimplementedError(Error* error, std::string_view what);

  void SetFeatures();
  bool EnsureVertexBufferSpace(u32 vertex_size, u32 vertex_count, Error* error = nullptr);
  bool EnsureIndexBufferSpace(u32 index_count, Error* error = nullptr);
  static u32 GetConstantRegisterCount(u32 size);
  void ApplyUniforms(const void* push_constants, u32 push_constants_size);
  void ApplyPipelineState(D3D9Pipeline* pipeline);

  ComPtr<IDirect3D9> m_d3d;
  ComPtr<IDirect3DDevice9> m_device;
  ComPtr<IDirect3DSurface9> m_backbuffer;
  ComPtr<IDirect3DVertexBuffer9> m_vertex_buffer;
  ComPtr<IDirect3DIndexBuffer9> m_index_buffer;
  ComPtr<IDirect3DVertexBuffer9> m_screen_quad_vertex_buffer;
  D3DPRESENT_PARAMETERS m_present_params = {};
  std::vector<u8> m_uniform_buffer_data;
  u32 m_vertex_buffer_size = 0;
  u32 m_index_buffer_size = 0;
  u32 m_vertex_buffer_position = 0;
  u32 m_index_buffer_position = 0;
  u32 m_current_base_vertex = 0;
  u32 m_current_base_index = 0;
  D3D9Pipeline* m_current_pipeline = nullptr;
};