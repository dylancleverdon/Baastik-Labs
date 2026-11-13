//! GPU context plus small helpers that keep pipeline boilerplate out of the scenes.

use anyhow::{anyhow, Result};
use std::borrow::Cow;

/// Format of every scene's render target (linear HDR).
pub const HDR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba16Float;

pub struct Gpu {
    pub adapter: wgpu::Adapter,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    pub adapter_info: wgpu::AdapterInfo,
    pub limits: wgpu::Limits,
}

impl Gpu {
    /// Picks an adapter (compatible with `surface` when given) and opens a device with the limits
    /// the scenes need.
    pub async fn new(instance: &wgpu::Instance, surface: Option<&wgpu::Surface<'_>>) -> Result<Self> {
        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: surface,
                force_fallback_adapter: false,
                apply_limit_buckets: false,
            })
            .await
            .map_err(|e| anyhow!("no GPU adapter: {e}"))?;
        let adapter_limits = adapter.limits();
        let mut limits = wgpu::Limits::default();
        // Big particle buffers: take what the adapter offers (Apple GPUs allow GBs).
        limits.max_storage_buffer_binding_size = adapter_limits.max_storage_buffer_binding_size;
        limits.max_buffer_size = adapter_limits.max_buffer_size;
        limits.max_texture_dimension_2d = adapter_limits.max_texture_dimension_2d.min(16384);
        limits.max_compute_workgroup_storage_size = adapter_limits.max_compute_workgroup_storage_size;
        limits.max_storage_buffers_per_shader_stage =
            adapter_limits.max_storage_buffers_per_shader_stage.min(16);
        let features = adapter.features() & wgpu::Features::FLOAT32_FILTERABLE;
        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("cymatica"),
                required_features: features,
                required_limits: limits.clone(),
                ..Default::default()
            })
            .await
            .map_err(|e| anyhow!("could not open the GPU: {e}"))?;
        Ok(Self { adapter_info: adapter.get_info(), adapter, device, queue, limits })
    }

    /// A device with no window (offline renders, tests).
    pub async fn headless() -> Result<(wgpu::Instance, Self)> {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle_from_env());
        let gpu = Self::new(&instance, None).await?;
        Ok((instance, gpu))
    }

    pub fn shader(&self, label: &str, source: String) -> wgpu::ShaderModule {
        self.device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some(label),
            source: wgpu::ShaderSource::Wgsl(Cow::Owned(source)),
        })
    }

    pub fn buffer(&self, label: &str, size: u64, usage: wgpu::BufferUsages) -> wgpu::Buffer {
        self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some(label),
            size: size.max(16),
            usage,
            mapped_at_creation: false,
        })
    }

    pub fn uniform_buffer(&self, label: &str, size: u64) -> wgpu::Buffer {
        self.buffer(label, size, wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST)
    }

    pub fn storage_buffer(&self, label: &str, size: u64) -> wgpu::Buffer {
        self.buffer(
            label,
            size,
            wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::COPY_SRC,
        )
    }

    pub fn texture(
        &self,
        label: &str,
        size: (u32, u32),
        format: wgpu::TextureFormat,
        usage: wgpu::TextureUsages,
    ) -> wgpu::Texture {
        self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some(label),
            size: wgpu::Extent3d { width: size.0.max(1), height: size.1.max(1), depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format,
            usage,
            view_formats: &[],
        })
    }

    /// A texture scenes render into and sample from.
    pub fn render_texture(&self, label: &str, size: (u32, u32), format: wgpu::TextureFormat) -> wgpu::Texture {
        self.texture(
            label,
            size,
            format,
            wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_SRC
                | wgpu::TextureUsages::COPY_DST,
        )
    }

    pub fn sampler(&self, filter: wgpu::FilterMode, address: wgpu::AddressMode) -> wgpu::Sampler {
        self.device.create_sampler(&wgpu::SamplerDescriptor {
            label: None,
            address_mode_u: address,
            address_mode_v: address,
            address_mode_w: address,
            mag_filter: filter,
            min_filter: filter,
            ..Default::default()
        })
    }

    pub fn bind_group(
        &self,
        label: &str,
        layout: &wgpu::BindGroupLayout,
        resources: &[wgpu::BindingResource<'_>],
    ) -> wgpu::BindGroup {
        let entries: Vec<wgpu::BindGroupEntry> = resources
            .iter()
            .enumerate()
            .map(|(i, r)| wgpu::BindGroupEntry { binding: i as u32, resource: r.clone() })
            .collect();
        self.device.create_bind_group(&wgpu::BindGroupDescriptor { label: Some(label), layout, entries: &entries })
    }

    /// Bind group layout from a list of binding kinds (bindings numbered 0..).
    pub fn layout(&self, label: &str, kinds: &[Bind]) -> wgpu::BindGroupLayout {
        let entries: Vec<wgpu::BindGroupLayoutEntry> = kinds
            .iter()
            .enumerate()
            .map(|(i, k)| wgpu::BindGroupLayoutEntry {
                binding: i as u32,
                // writable storage isn't allowed in vertex shaders
                visibility: match k {
                    Bind::StorageRw | Bind::StorageTexture(_) => {
                        wgpu::ShaderStages::FRAGMENT | wgpu::ShaderStages::COMPUTE
                    }
                    _ => wgpu::ShaderStages::VERTEX_FRAGMENT | wgpu::ShaderStages::COMPUTE,
                },
                ty: k.ty(),
                count: None,
            })
            .collect();
        self.device
            .create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor { label: Some(label), entries: &entries })
    }

    pub fn pipeline_layout(&self, label: &str, groups: &[&wgpu::BindGroupLayout]) -> wgpu::PipelineLayout {
        let groups: Vec<Option<&wgpu::BindGroupLayout>> = groups.iter().map(|g| Some(*g)).collect();
        self.device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some(label),
            bind_group_layouts: &groups,
            immediate_size: 0,
        })
    }

    pub fn compute_pipeline(
        &self,
        label: &str,
        module: &wgpu::ShaderModule,
        entry: &str,
        layout: &wgpu::PipelineLayout,
    ) -> wgpu::ComputePipeline {
        self.device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some(label),
            layout: Some(layout),
            module,
            entry_point: Some(entry),
            compilation_options: Default::default(),
            cache: None,
        })
    }

    /// Fullscreen-triangle pipeline (vertex entry `vs_fullscreen` from the common prelude).
    pub fn fullscreen_pipeline(
        &self,
        label: &str,
        module: &wgpu::ShaderModule,
        fs_entry: &str,
        layout: &wgpu::PipelineLayout,
        format: wgpu::TextureFormat,
        blend: Option<wgpu::BlendState>,
    ) -> wgpu::RenderPipeline {
        self.render_pipeline(label, module, "vs_fullscreen", fs_entry, layout, format, blend, wgpu::PrimitiveTopology::TriangleList)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render_pipeline(
        &self,
        label: &str,
        module: &wgpu::ShaderModule,
        vs_entry: &str,
        fs_entry: &str,
        layout: &wgpu::PipelineLayout,
        format: wgpu::TextureFormat,
        blend: Option<wgpu::BlendState>,
        topology: wgpu::PrimitiveTopology,
    ) -> wgpu::RenderPipeline {
        self.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some(label),
            layout: Some(layout),
            vertex: wgpu::VertexState {
                module,
                entry_point: Some(vs_entry),
                compilation_options: Default::default(),
                buffers: &[],
            },
            primitive: wgpu::PrimitiveState { topology, ..Default::default() },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            fragment: Some(wgpu::FragmentState {
                module,
                entry_point: Some(fs_entry),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState { format, blend, write_mask: wgpu::ColorWrites::ALL })],
            }),
            multiview_mask: None,
            cache: None,
        })
    }
}

/// Binding kinds for `Gpu::layout`.
#[derive(Clone, Copy, Debug)]
pub enum Bind {
    Uniform,
    StorageRead,
    StorageRw,
    /// Filterable float texture.
    Texture,
    /// Non-filterable float texture (e.g. r32float without FLOAT32_FILTERABLE).
    TextureUnfilterable,
    Sampler,
    StorageTexture(wgpu::TextureFormat),
}

impl Bind {
    fn ty(self) -> wgpu::BindingType {
        match self {
            Bind::Uniform => wgpu::BindingType::Buffer {
                ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false,
                min_binding_size: None,
            },
            Bind::StorageRead => wgpu::BindingType::Buffer {
                ty: wgpu::BufferBindingType::Storage { read_only: true },
                has_dynamic_offset: false,
                min_binding_size: None,
            },
            Bind::StorageRw => wgpu::BindingType::Buffer {
                ty: wgpu::BufferBindingType::Storage { read_only: false },
                has_dynamic_offset: false,
                min_binding_size: None,
            },
            Bind::Texture => wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: true },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: false,
            },
            Bind::TextureUnfilterable => wgpu::BindingType::Texture {
                sample_type: wgpu::TextureSampleType::Float { filterable: false },
                view_dimension: wgpu::TextureViewDimension::D2,
                multisampled: false,
            },
            Bind::Sampler => wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
            Bind::StorageTexture(format) => wgpu::BindingType::StorageTexture {
                access: wgpu::StorageTextureAccess::WriteOnly,
                format,
                view_dimension: wgpu::TextureViewDimension::D2,
            },
        }
    }
}

/// Starts a render pass that clears (or loads) a single colour target.
pub fn pass<'e>(
    encoder: &'e mut wgpu::CommandEncoder,
    label: &str,
    view: &wgpu::TextureView,
    clear: Option<wgpu::Color>,
) -> wgpu::RenderPass<'e> {
    encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
        label: Some(label),
        color_attachments: &[Some(wgpu::RenderPassColorAttachment {
            view,
            depth_slice: None,
            resolve_target: None,
            ops: wgpu::Operations {
                load: match clear {
                    Some(c) => wgpu::LoadOp::Clear(c),
                    None => wgpu::LoadOp::Load,
                },
                store: wgpu::StoreOp::Store,
            },
        })],
        depth_stencil_attachment: None,
        timestamp_writes: None,
        occlusion_query_set: None,
        multiview_mask: None,
    })
}

/// Draws a fullscreen triangle with `pipeline` and bind groups into `view`.
pub fn fullscreen(
    encoder: &mut wgpu::CommandEncoder,
    label: &str,
    view: &wgpu::TextureView,
    pipeline: &wgpu::RenderPipeline,
    groups: &[&wgpu::BindGroup],
    clear: Option<wgpu::Color>,
) {
    let mut rp = pass(encoder, label, view, clear);
    rp.set_pipeline(pipeline);
    for (i, g) in groups.iter().enumerate() {
        rp.set_bind_group(i as u32, *g, &[]);
    }
    rp.draw(0..3, 0..1);
}

pub const ADDITIVE: wgpu::BlendState = wgpu::BlendState {
    color: wgpu::BlendComponent {
        src_factor: wgpu::BlendFactor::One,
        dst_factor: wgpu::BlendFactor::One,
        operation: wgpu::BlendOperation::Add,
    },
    alpha: wgpu::BlendComponent {
        src_factor: wgpu::BlendFactor::One,
        dst_factor: wgpu::BlendFactor::One,
        operation: wgpu::BlendOperation::Add,
    },
};

pub const PREMULTIPLIED: wgpu::BlendState = wgpu::BlendState::PREMULTIPLIED_ALPHA_BLENDING;
