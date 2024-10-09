#include "webgpu-utils.h"

#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif // __EMSCRIPTEN__

#include <iostream>
#include <cassert>
#include <vector>

class Application {
public:
	// Initialize everything and return true if it went all right
	bool Initialize();

	// Uninitialize everything that was initialized
	void Terminate();

	// Draw a frame and handle events
	void MainLoop();

	// Return true as long as the main loop should keep on running
	bool IsRunning();

private:
	// We put here all the variables that are shared between init and main loop
	GLFWwindow *window;
	WGPUDevice device;
	WGPUQueue queue;
	WGPUSurface surface;
    WGPUTextureView GetNextSurfaceTextureView();

};

int main() {
	Application app;

	if (!app.Initialize()) {
		return 1;
	}

#ifdef __EMSCRIPTEN__
	// Equivalent of the main loop when using Emscripten:
	auto callback = [](void *arg) {
		//                   ^^^ 2. We get the address of the app in the callback.
		Application* pApp = reinterpret_cast<Application*>(arg);
		//                  ^^^^^^^^^^^^^^^^ 3. We force this address to be interpreted
		//                                      as a pointer to an Application object.
		pApp->MainLoop(); // 4. We can use the application object
	};
	emscripten_set_main_loop_arg(callback, &app, 0, true);
	//                                     ^^^^ 1. We pass the address of our application object.
#else // __EMSCRIPTEN__
	while (app.IsRunning()) {
		app.MainLoop();
	}
#endif // __EMSCRIPTEN__

	app.Terminate();

	return 0;
}

bool Application::Initialize() {
	// Move the whole initialization here
	// Open window
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // <-- extra info for glfwCreateWindow
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

	// Create instance
	WGPUInstance instance = wgpuCreateInstance(nullptr);

	// Get adapter
	std::cout << "Requesting adapter..." << std::endl;
	surface = glfwGetWGPUSurface(instance, window);

	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = nullptr;
	adapterOpts.compatibleSurface = surface;
	//                              ^^^^^^^ Use the surface here

	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;

	// We no longer need to access the instance
	wgpuInstanceRelease(instance);

	// Get device
	std::cout << "Requesting device..." << std::endl;
	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = nullptr;
	deviceDesc.label = "My Device"; // anything works here, that's your call
	deviceDesc.requiredFeatureCount = 0; // we do not require any specific feature
	deviceDesc.requiredLimits = nullptr; // we do not require any specific limit
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = "The default queue";
	// A function that is invoked whenever the device stops being available.
	deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	};
	device = requestDeviceSync(adapter, &deviceDesc);
	std::cout << "Got device: " << device << std::endl;

	// We no longer need to access the adapter

	// Device error callback
	auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
		std::cout << "Uncaptured device error: type " << type;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	};
	wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);

	queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;

    // Configuration of the textures created for the underlying swap chain
    config.width = 640;
    config.height = 480;

    WGPUTextureFormat surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
	wgpuAdapterRelease(adapter);
    config.format = surfaceFormat;
    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;

    config.usage = WGPUTextureUsage_RenderAttachment;
    config.device = device;

    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface, &config);

	return true;
}

void Application::Terminate() {
	// Move all the release/destroy/terminate calls here
	wgpuQueueRelease(queue);
    wgpuSurfaceUnconfigure(surface);
	wgpuSurfaceRelease(surface);
	wgpuDeviceRelease(device);
	glfwDestroyWindow(window);
	glfwTerminate();
}

void Application::MainLoop() {
	glfwPollEvents();

	// Get the next target texture view
	WGPUTextureView targetView = GetNextSurfaceTextureView();
	if (!targetView) return;

	// Create a command encoder for the draw call
	WGPUCommandEncoderDescriptor encoderDesc = {};
	encoderDesc.nextInChain = nullptr;
	encoderDesc.label = "My command encoder";
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	// Create the render pass that clears the screen with our color
	WGPURenderPassDescriptor renderPassDesc = {};
	renderPassDesc.nextInChain = nullptr;

	// The attachment part of the render pass descriptor describes the target texture of the pass
	WGPURenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
	renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
	renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
    #ifndef WEBGPU_BACKEND_WGPU
        renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    #endif // NOT WEBGPU_BACKEND_WGPU

	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;
	renderPassDesc.depthStencilAttachment = nullptr;
	renderPassDesc.timestampWrites = nullptr;

	// Create the render pass and end it immediately (we only clear the screen but do not draw anything)
	WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
	wgpuRenderPassEncoderEnd(renderPass);
	wgpuRenderPassEncoderRelease(renderPass);

	// Finally encode and submit the render pass
	WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
	cmdBufferDescriptor.nextInChain = nullptr;
	cmdBufferDescriptor.label = "Command buffer";
	WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
	wgpuCommandEncoderRelease(encoder);

	std::cout << "Submitting command..." << std::endl;
	wgpuQueueSubmit(queue, 1, &command);
	wgpuCommandBufferRelease(command);
	std::cout << "Command submitted." << std::endl;

	// At the end of the frame
	wgpuTextureViewRelease(targetView);
    #ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(surface);
    #endif

    #if defined(WEBGPU_BACKEND_DAWN)
        wgpuDeviceTick(device);
    #elif defined(WEBGPU_BACKEND_WGPU)
        wgpuDevicePoll(device, false, nullptr);
    #endif
}

bool Application::IsRunning() {
	return !glfwWindowShouldClose(window);
}

WGPUTextureView Application::GetNextSurfaceTextureView() {

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = "Surface texture view";
    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    return targetView;
}
