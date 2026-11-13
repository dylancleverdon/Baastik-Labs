//! Every WGSL module must parse, validate and translate to Metal (MSL) and SPIR-V. This catches
//! shader errors without a GPU, including Metal-only translation problems, on any machine.

use cymatica_engine::shaders::all_shader_sources;

#[test]
fn every_shader_validates_and_translates_to_metal() {
    let mut failures = Vec::new();
    for (name, src) in all_shader_sources() {
        let module = match naga::front::wgsl::parse_str(&src) {
            Ok(m) => m,
            Err(e) => {
                failures.push(format!("{name}: parse error\n{}", e.emit_to_string(&src)));
                continue;
            }
        };
        let info = match naga::valid::Validator::new(naga::valid::ValidationFlags::all(), naga::valid::Capabilities::all())
            .validate(&module)
        {
            Ok(i) => i,
            Err(e) => {
                failures.push(format!("{name}: validation error\n{}", e.emit_to_string(&src)));
                continue;
            }
        };
        let options = naga::back::msl::Options { lang_version: (2, 1), ..Default::default() };
        let pipeline_options = naga::back::msl::PipelineOptions::default();
        if let Err(e) = naga::back::msl::write_string(&module, &info, &options, &pipeline_options) {
            failures.push(format!("{name}: MSL translation failed: {e:?}"));
        }
        if let Err(e) = naga::back::spv::write_vec(&module, &info, &Default::default(), None) {
            failures.push(format!("{name}: SPIR-V translation failed: {e:?}"));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n\n"));
}
