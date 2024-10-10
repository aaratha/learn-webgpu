#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_glfw.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <iostream>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
};

struct Uniforms {
    glm::mat4 modelViewProj;
};

const char* shaderSource = R"(
struct Uniforms {
    modelViewProj: mat4x4<f32>,
}
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uniforms.modelViewProj * vec4<f32>(in.position, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(in.color, 1.0);
}
)";

class RotatingCube {
public:
    RotatingCube() {
        InitWindow();
        InitWebGPU();
        CreatePipeline();
        CreateVertexBuffer();
        CreateUniformBuffer();
    }

    void Run() {
        while (!glfwWindowShouldClose(window)) {
            Update();
            Render();
            glfwPollEvents();
        }
    }

private:
    GLFWwindow* window;
    wgpu::Instance instance;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer uniformBuffer;
    wgpu::BindGroup bindGroup;
    float rotation = 0.0f;

    void InitWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(800, 600, "WebGPU Rotating Cube", nullptr, nullptr);
    }

		void InitWebGPU() {
				instance = wgpu::CreateInstance();
				if (!instance) {
						std::cerr << "Could not initialize WebGPU!" << std::endl;
						return;
				}

				surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);

				wgpu::RequestAdapterOptions adapterOpts = {};
				adapterOpts.compatibleSurface = surface;

				wgpu::Adapter adapter;
				instance.RequestAdapter(
						&adapterOpts,
						[](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
								*reinterpret_cast<wgpu::Adapter*>(userdata) =
										wgpu::Adapter::Acquire(adapter);
						},
						&adapter
				);

				wgpu::DeviceDescriptor deviceDesc = {};
				adapter.RequestDevice(
						&deviceDesc,
						[](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
								*reinterpret_cast<wgpu::Device*>(userdata) =
										wgpu::Device::Acquire(device);
						},
						&device
				);

				queue = device.GetQueue();

				// Get the surface capabilities and preferred format
				wgpu::SurfaceCapabilities caps;
				surface.GetCapabilities(adapter, &caps);

				// Configure the surface
				wgpu::SurfaceConfiguration config = {};
				config.device = device;
				config.format = caps.formats[0]; // Use the first supported format
				config.usage = wgpu::TextureUsage::RenderAttachment;
				config.alphaMode = wgpu::CompositeAlphaMode::Auto;
				config.viewFormatCount = 0;
				config.viewFormats = nullptr;

				// Set the initial size
				int width, height;
				glfwGetWindowSize(window, &width, &height);
				config.width = static_cast<uint32_t>(width);
				config.height = static_cast<uint32_t>(height);

				// Configure the surface with our settings
				surface.Configure(&config);

				// Also handle resize events
				glfwSetWindowUserPointer(window, this);
				glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
						auto app = reinterpret_cast<RotatingCube*>(glfwGetWindowUserPointer(window));
						app->HandleResize(width, height);
				});
		}

		// Add this method to handle window resizing
		void HandleResize(int width, int height) {
				if (width == 0 || height == 0) return; // Skip if minimized

				// Reconfigure the surface with the new size
				wgpu::SurfaceConfiguration config = {};
				config.device = device;

				wgpu::SurfaceCapabilities caps;
				surface.GetCapabilities(device.GetAdapter(), &caps);

				config.format = caps.formats[0];
				config.usage = wgpu::TextureUsage::RenderAttachment;
				config.alphaMode = wgpu::CompositeAlphaMode::Auto;
				config.width = static_cast<uint32_t>(width);
				config.height = static_cast<uint32_t>(height);
				config.viewFormatCount = 0;
				config.viewFormats = nullptr;

				surface.Configure(&config);
		}

    void CreatePipeline() {
        // Create shader module
        wgpu::ShaderModuleWGSLDescriptor wgslDesc = {};
        wgslDesc.code = shaderSource;

        wgpu::ShaderModuleDescriptor shaderDesc = {};
        shaderDesc.nextInChain = &wgslDesc;

        auto shaderModule = device.CreateShaderModule(&shaderDesc);

        // Create bind group layout
        wgpu::BindGroupLayoutEntry bindingLayout = {};
        bindingLayout.binding = 0;
        bindingLayout.visibility = wgpu::ShaderStage::Vertex;
        bindingLayout.buffer.type = wgpu::BufferBindingType::Uniform;
        bindingLayout.buffer.minBindingSize = sizeof(Uniforms);

        wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc = {};
        bindGroupLayoutDesc.entryCount = 1;
        bindGroupLayoutDesc.entries = &bindingLayout;
        auto bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

        // Create pipeline layout
        wgpu::PipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
        auto pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);

        // Vertex state
        std::vector<wgpu::VertexAttribute> vertexAttribs = {
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, pos),
                .shaderLocation = 0,
            },
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, color),
                .shaderLocation = 1,
            },
        };

        wgpu::VertexBufferLayout vertexBufferLayout = {};
        vertexBufferLayout.arrayStride = sizeof(Vertex);
        vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
        vertexBufferLayout.attributeCount = vertexAttribs.size();
        vertexBufferLayout.attributes = vertexAttribs.data();

        // Color target state
        wgpu::ColorTargetState colorTarget = {};
				wgpu::SurfaceCapabilities caps;
				surface.GetCapabilities(device.GetAdapter(), &caps);
				colorTarget.format = caps.formats[0];
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragmentState = {};
        fragmentState.module = shaderModule;
        fragmentState.entryPoint = "fs_main";
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        // Create pipeline
        wgpu::RenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = "vs_main";
        pipelineDesc.vertex.bufferCount = 1;
        pipelineDesc.vertex.buffers = &vertexBufferLayout;
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = wgpu::CullMode::Back;

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
    }

    void CreateVertexBuffer() {
        std::array<Vertex, 24> vertices = {
            // Front face
            Vertex{{-1, -1,  1}, {1, 0, 0}},
            Vertex{{ 1, -1,  1}, {1, 0, 0}},
            Vertex{{ 1,  1,  1}, {1, 0, 0}},
            Vertex{{-1,  1,  1}, {1, 0, 0}},
            // Back face
            Vertex{{-1, -1, -1}, {0, 1, 0}},
            Vertex{{-1,  1, -1}, {0, 1, 0}},
            Vertex{{ 1,  1, -1}, {0, 1, 0}},
            Vertex{{ 1, -1, -1}, {0, 1, 0}},
            // Other faces...
        };

        wgpu::BufferDescriptor bufferDesc = {};
        bufferDesc.size = sizeof(vertices);
        bufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

        vertexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(vertexBuffer, 0, vertices.data(), bufferDesc.size);
    }

    void CreateUniformBuffer() {
        wgpu::BufferDescriptor bufferDesc = {};
        bufferDesc.size = sizeof(Uniforms);
        bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

        uniformBuffer = device.CreateBuffer(&bufferDesc);

        wgpu::BindGroupLayout bindGroupLayout = pipeline.GetBindGroupLayout(0);

        wgpu::BindGroupEntry bindGroupEntry = {};
        bindGroupEntry.binding = 0;
        bindGroupEntry.buffer = uniformBuffer;
        bindGroupEntry.size = sizeof(Uniforms);

        wgpu::BindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.layout = bindGroupLayout;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &bindGroupEntry;

        bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    void Update() {
        rotation += 0.01f;

        glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(1.0f, 1.0f, 0.0f));
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, -5.0f),
                                   glm::vec3(0.0f),
                                   glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

        Uniforms uniforms = {
            .modelViewProj = proj * view * model
        };

        queue.WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(uniforms));
    }

    void Render() {
        wgpu::SurfaceTexture surfaceTexture;
        surface.GetCurrentTexture(&surfaceTexture);

        wgpu::TextureViewDescriptor viewDesc = {};
        wgpu::TextureView backbufferView = surfaceTexture.texture.CreateView(&viewDesc);

        wgpu::RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = backbufferView;
        colorAttachment.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;

        wgpu::RenderPassDescriptor renderPassDesc = {};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDesc);

        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroup);
        pass.SetVertexBuffer(0, vertexBuffer);
        pass.Draw(24, 1, 0, 0);

        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        surface.Present();
    }
};

int main() {
    RotatingCube app;
    app.Run();
    return 0;
}
