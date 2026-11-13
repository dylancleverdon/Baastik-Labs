//! Diagnostic: lists GPU adapters and runs a tiny compute + render pass headlessly.
//! Prints `GPU_PROBE_OK <backend> <name>` or `GPU_PROBE_NO_ADAPTER`.

fn main() {
    pollster::block_on(async {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle_from_env());
        for a in instance.enumerate_adapters(wgpu::Backends::all()).await {
            println!("adapter: {:?}", a.get_info());
        }
        let Ok(adapter) = instance.request_adapter(&wgpu::RequestAdapterOptions::default()).await else {
            println!("GPU_PROBE_NO_ADAPTER");
            return;
        };
        let info = adapter.get_info();
        println!("limits: {:?}", adapter.limits());
        let (device, queue) = adapter.request_device(&wgpu::DeviceDescriptor::default()).await.expect("device");
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: None,
            source: wgpu::ShaderSource::Wgsl(
                r#"
@group(0) @binding(0) var<storage, read_write> data: array<atomic<i32>>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    atomicAdd(&data[id.x % 4u], i32(id.x));
}
"#
                .into(),
            ),
        });
        let buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: None,
            size: 16,
            usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
            mapped_at_creation: false,
        });
        let read = device.create_buffer(&wgpu::BufferDescriptor {
            label: None,
            size: 16,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: None,
            layout: None,
            module: &shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });
        let bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: None,
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[wgpu::BindGroupEntry { binding: 0, resource: buf.as_entire_binding() }],
        });
        let mut enc = device.create_command_encoder(&Default::default());
        {
            let mut pass = enc.begin_compute_pass(&Default::default());
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bg, &[]);
            pass.dispatch_workgroups(16, 1, 1);
        }
        enc.copy_buffer_to_buffer(&buf, 0, &read, 0, 16);
        queue.submit([enc.finish()]);
        read.slice(..).map_async(wgpu::MapMode::Read, |r| r.unwrap());
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        let v: Vec<i32> = bytemuck::cast_slice(&read.slice(..).get_mapped_range().unwrap()).to_vec();
        assert_eq!(v, vec![130560, 130816, 131072, 131328], "compute result");
        println!("GPU_PROBE_OK {:?} {}", info.backend, info.name);
    });
}
