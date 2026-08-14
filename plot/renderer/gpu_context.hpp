#pragma once

/**
 * @file gpu_context.hpp
 * @brief The renderer's GPU backend switch + bring-up: one MINIMAL device-context surface,
 *        two implementations — Vulkan (SPIR-V from slangc) and Metal (real on Apple, the
 *        software-emulated device + C++ stand-ins everywhere else).
 *
 * The raster step drives the device through a tiny context contract — `new_buffer` / `upload` /
 * `download` / `release_buffer` and a blocking `dispatch_2d` over the two plot kernels — and
 * never names a backend. This is the same context pattern cheatah-gpu's compute consumers have
 * proven out, trimmed to exactly what a rasterizer needs: no pooling, no staging heuristics,
 * host-visible storage buffers and serial blocking dispatches.
 *
 * The build defines a lane when its stack is available (see CMakeLists.txt):
 *   - `CHEATAH_PLOT_GPU_VULKAN` — vulkan_context-style bring-up over cheatah-gpu's `vk.*`
 *     forwarders (volk-loaded, so nothing links libvulkan); kernels load as SPIR-V from
 *     `CHEATAH_PLOT_SPV_DIR` (the same-named environment variable takes precedence).
 *     `CHEATAH_PLOT_VK_DEVICE` forces a device by name substring or zero-based index.
 *   - `CHEATAH_PLOT_GPU_METAL` — metal_context-style bring-up over cheatah-gpu's `mtl.*`
 *     surface. On Apple the runtime compiles the slang-generated MSL from
 *     `CHEATAH_PLOT_MSL_DIR`; off Apple cheatah-gpu's software-emulated Metal device runs the
 *     registered C++ stand-ins below, which call raster_cpu.hpp's own coverage/blend helpers —
 *     BIT-EXACT with the reference rasterizer by construction.
 *
 * The platform-default lane (Metal on Apple, Vulkan elsewhere — cheatah-gpu's backend rule) is
 * what `render()` and @ref gpu_available drive; both lanes stay reachable for the dual-lane
 * test matrix via @ref detail::ctx_of.
 */

#if !defined(CHEATAH_PLOT_GPU_VULKAN) && !defined(CHEATAH_PLOT_GPU_METAL)
#error "gpu_context.hpp needs a GPU lane (CHEATAH_PLOT_GPU_VULKAN / CHEATAH_PLOT_GPU_METAL)"
#endif

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "kernels.hpp"
#include "raster_cpu.hpp"

#if defined(CHEATAH_PLOT_GPU_VULKAN)
#include "gpu/dispatch/dispatch.hpp"
#include "gpu/vulkan/commands.hpp"
#endif
#if defined(CHEATAH_PLOT_GPU_METAL)
#include "gpu/metal/types.hpp"
#if !defined(__APPLE__)
#include "gpu/metal/emulated/emulated.hpp"
#endif
#endif

namespace cheatah::plot::renderer {

/// Threads per workgroup axis of BOTH kernels — matches `[numthreads(16, 16, 1)]` in
/// shaders/plot.slang. Equal to @ref kTile ON PURPOSE: one workgroup spans one raster tile, so
/// every thread in a group walks the same tile bin.
inline constexpr std::uint32_t kLocal2d = 16;

/// The clear kernel's entry name — `plot_clear` in shaders/plot.slang (buffers: fb, params).
inline constexpr const char* kClearKernel = "plot_clear";
/// The raster kernel's entry name — `plot_raster` in shaders/plot.slang (buffers: prims,
/// tile_offsets, tile_prims, fb, params).
inline constexpr const char* kRasterKernel = "plot_raster";

namespace detail {

/// The smallest device buffer either lane allocates: requests are floored here so empty draw
/// lists (a figure with no primitives) still bind a valid buffer. @complexity O(1). @alloc none.
inline constexpr std::size_t kMinBufferBytes = 16;

#if defined(CHEATAH_PLOT_GPU_METAL) && !defined(__APPLE__)

namespace emu = cheatah::gpu::metal::emulated;

/// CPU stand-in for `plot_clear` on the emulated Metal device: writes params.clear into every
/// framebuffer pixel, exactly like the kernel's guarded one-thread-per-pixel store.
/// @param b The bound buffer pointers in kernel binding order: {fb, params}.
/// @param n The binding count (no-op when fewer than expected).
/// @param shape The dispatch thread grid (its width/height bound the loops).
/// @complexity O(pixels).
/// @alloc none — writes in place through the bound buffers. @gpualloc none.
/// @test plot:gpu_raster
inline void plot_clear_emulated(void** b, unsigned n, const emu::DispatchShape& shape) {
    if (n < 2 || b[0] == nullptr || b[1] == nullptr) return;
    std::uint32_t* fb = static_cast<std::uint32_t*>(b[0]);
    const std::uint32_t* params = static_cast<const std::uint32_t*>(b[1]);
    const std::uint32_t width = params[0], height = params[1], clear = params[3];
    for (unsigned long y = 0; y < shape.threads.height && y < height; ++y)
        for (unsigned long x = 0; x < shape.threads.width && x < width; ++x)
            fb[static_cast<std::size_t>(y) * width + x] = clear;
}

/// CPU stand-in for `plot_raster` on the emulated Metal device: loops the pixels calling the
/// reference rasterizer's OWN detail::coverage / detail::quantize_cov / detail::blend_over, so
/// this lane is bit-exact with @ref raster_cpu by construction — it IS the same code.
/// @param b The bound buffer pointers in kernel binding order:
///          {prims, tile_offsets, tile_prims, fb, params}.
/// @param n The binding count (no-op when fewer than expected).
/// @param shape The dispatch thread grid (its width/height bound the loops).
/// @complexity O(pixels + Σ per-tile prim work).
/// @alloc none — writes in place through the bound buffers. @gpualloc none.
/// @test plot:gpu_raster
inline void plot_raster_emulated(void** b, unsigned n, const emu::DispatchShape& shape) {
    if (n < 5) return;
    for (unsigned i = 0; i < 5; ++i)
        if (b[i] == nullptr) return;
    const Prim* prims = static_cast<const Prim*>(b[0]);
    const std::uint32_t* offsets = static_cast<const std::uint32_t*>(b[1]);
    const std::uint32_t* indices = static_cast<const std::uint32_t*>(b[2]);
    std::uint32_t* fb = static_cast<std::uint32_t*>(b[3]);
    const std::uint32_t* params = static_cast<const std::uint32_t*>(b[4]);
    const std::uint32_t width = params[0], height = params[1], tiles_x = params[2];
    for (unsigned long y = 0; y < shape.threads.height && y < height; ++y) {
        for (unsigned long x = 0; x < shape.threads.width && x < width; ++x) {
            const std::size_t tile =
                static_cast<std::size_t>(y / kTile) * tiles_x + (x / kTile);
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            std::uint32_t pixel = params[3];
            for (std::uint32_t k = offsets[tile]; k < offsets[tile + 1]; ++k) {
                const Prim& p = prims[indices[k]];
                const std::uint32_t cov8 = quantize_cov(coverage(p, px, py));
                if (cov8 != 0u) pixel = blend_over(pixel, p.rgba, cov8);
            }
            fb[static_cast<std::size_t>(y) * width + x] = pixel;
        }
    }
}

/// Register both raster stand-ins with the software Metal device, keyed by kernel name — the
/// same registration mechanism every emulated-Metal consumer uses. Idempotent.
/// @complexity O(1). @alloc none. @gpualloc none.
/// @test plot:gpu_raster
inline void register_emulated_kernels() {
    emu::register_kernel(kClearKernel, &plot_clear_emulated);
    emu::register_kernel(kRasterKernel, &plot_raster_emulated);
}

#endif  // CHEATAH_PLOT_GPU_METAL && !__APPLE__

#if defined(CHEATAH_PLOT_GPU_VULKAN)

/// Short alias for the cheatah-gpu Vulkan surface (the volk-loaded `vk.*` forwarders).
namespace vkc = cheatah::gpu::vulkan;

/// The Vulkan-lane buffer object the raster step passes around (opaque outside the context): a
/// storage buffer in host-visible, host-coherent memory, persistently mapped.
struct VulkanBuffer {
    VkBuffer buf = VK_NULL_HANDLE;        ///< the storage buffer handle.
    VkDeviceMemory mem = VK_NULL_HANDLE;  ///< its backing allocation.
    void* map = nullptr;                  ///< the persistent host mapping.
    VkDeviceSize bytes = 0;               ///< the allocated size in bytes.
};

/**
 * Process-wide Vulkan compute context for the plot kernels — instance + device + queue, one
 * shared 5-slot descriptor layout, per-kernel pipelines built from the SPIR-V slangc emitted at
 * build time, and blocking 2-D dispatch. Constructed lazily on first use via
 * @ref ctx_of; lives for the process. Throws std::runtime_error on any bring-up failure — the
 * caller (`gpu_available` / `render`) treats that as "no GPU" and falls back.
 */
class VulkanContext {
public:
    /// The backend-native buffer handle type the context contract deals in.
    using buffer_t = VulkanBuffer;

    /// Storage-buffer slots in the shared descriptor layout — plot_raster's five bindings.
    static constexpr std::uint32_t kMaxBindings = 5;

    /// Bring up the whole lane: volk, instance, the highest-scoring compute device (or the
    /// `CHEATAH_PLOT_VK_DEVICE` override), queue, descriptor + command machinery.
    /// @complexity O(devices) enumeration + driver bring-up.
    /// @alloc the context's Vulkan handles. @gpualloc none until the first buffer.
    /// @test plot:gpu_raster
    VulkanContext() {
#if defined(VOLK_H_)
        // The volk lane (no libvulkan link anywhere): bring the loader up in-process. Failing
        // HERE means the machine has no Vulkan loader/driver at all.
        static const VkResult volk_ok = volkInitialize();
        if (volk_ok != VK_SUCCESS)
            throw std::runtime_error("cheatah-plot vulkan: no Vulkan loader (volkInitialize failed)");
#endif
        create_instance();
#if defined(VOLK_H_)
        volkLoadInstance(instance_);
#endif
        pick_physical_device();
        create_device();
        create_descriptor_machinery();
        create_command_machinery();
    }

    /// Tear the lane down in reverse bring-up order (buffers are caller-released; dispatches
    /// are blocking, so nothing is in flight). @complexity O(pipelines). @alloc none.
    /// @gpualloc frees every context-owned device object.
    ~VulkanContext() {
        if (device_ != VK_NULL_HANDLE) {
            vkc::DeviceWaitIdle(device_);
            for (auto& [name, pipe] : pipelines_) vkc::DestroyPipeline(device_, pipe, nullptr);
            vkc::DestroyFence(device_, fence_, nullptr);
            vkc::DestroyCommandPool(device_, cmd_pool_, nullptr);
            vkc::DestroyDescriptorPool(device_, pool_, nullptr);
            vkc::DestroyPipelineLayout(device_, pipe_layout_, nullptr);
            vkc::DestroyDescriptorSetLayout(device_, set_layout_, nullptr);
            vkc::DestroyDevice(device_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE) vkc::DestroyInstance(instance_, nullptr);
    }
    VulkanContext(const VulkanContext&) = delete;             ///< process-wide: not copyable.
    VulkanContext& operator=(const VulkanContext&) = delete;  ///< process-wide: not assignable.

    /// Whether bring-up completed (the constructor throws otherwise, so this is true for any
    /// live instance — it exists for the shared probe contract). @complexity O(1). @alloc none.
    /// @test plot:gpu_raster
    [[nodiscard]] bool ok() const { return device_ != VK_NULL_HANDLE; }

    /// The selected physical device's name (test logs say which silicon actually ran).
    /// @return The driver-reported device name. @complexity O(1). @alloc none.
    /// @test plot:gpu_raster
    [[nodiscard]] const std::string& device_name() const { return device_name_; }

    /// A storage buffer of ≥ @p bytes bytes in host-visible, host-coherent memory, persistently
    /// mapped — a plot frame's buffers are small and one-shot, so there is no pooling here.
    /// @param bytes The requested size (floored to @ref kMinBufferBytes).
    /// @return The new buffer (caller releases via @ref release_buffer).
    /// @complexity O(1) plus driver allocation.
    /// @alloc the buffer box. @gpualloc one device allocation of the floored size.
    /// @test plot:gpu_raster
    [[nodiscard]] buffer_t* new_buffer(std::size_t bytes) {
        buffer_t* b = new buffer_t;
        b->bytes = bytes < kMinBufferBytes ? kMinBufferBytes : bytes;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = b->bytes;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkc::CreateBuffer(device_, &bi, nullptr, &b->buf), "CreateBuffer");
        VkMemoryRequirements req{};
        vkc::GetBufferMemoryRequirements(device_, b->buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = host_memory_type(req.memoryTypeBits);
        check(vkc::AllocateMemory(device_, &ai, nullptr, &b->mem), "AllocateMemory");
        check(vkc::BindBufferMemory(device_, b->buf, b->mem, 0), "BindBufferMemory");
        check(vkc::MapMemory(device_, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map), "MapMemory");
        return b;
    }

    /// Copy @p bytes of host memory into a buffer (host-coherent: a plain memcpy).
    /// @param dst The destination buffer. @param src The host source. @param bytes Bytes to copy.
    /// @complexity O(bytes). @alloc none. @gpualloc none (the mapping is persistent).
    /// @test plot:gpu_raster
    void upload(buffer_t* dst, const void* src, std::size_t bytes) {
        if (bytes > dst->bytes)
            throw std::runtime_error("cheatah-plot vulkan: upload exceeds buffer size");
        std::memcpy(dst->map, src, bytes);
    }

    /// Copy @p bytes of a buffer into host memory (the download mirror of @ref upload).
    /// @param src The source buffer. @param dst The host destination. @param bytes Bytes to copy.
    /// @complexity O(bytes). @alloc none. @gpualloc none.
    /// @test plot:gpu_raster
    void download(buffer_t* src, void* dst, std::size_t bytes) {
        if (bytes > src->bytes)
            throw std::runtime_error("cheatah-plot vulkan: download exceeds buffer size");
        std::memcpy(dst, src->map, bytes);
    }

    /// Destroy a buffer (dispatches are blocking, so nothing is in flight).
    /// @param b The buffer to release. @complexity O(1). @alloc frees the box.
    /// @gpualloc frees the device allocation.
    /// @test plot:gpu_raster
    void release_buffer(buffer_t* b) {
        vkc::UnmapMemory(device_, b->mem);
        vkc::DestroyBuffer(device_, b->buf, nullptr);
        vkc::FreeMemory(device_, b->mem, nullptr);
        delete b;
    }

    /// Bind @p count buffers at bindings 0..count-1 and run kernel @p name over a @p w × @p h
    /// pixel grid (workgroups of @ref kLocal2d²), blocking until complete.
    /// @param name The kernel entry name (@ref kClearKernel / @ref kRasterKernel).
    /// @param buffers The buffers, in the kernel's binding order.
    /// @param count How many bindings (≤ @ref kMaxBindings).
    /// @param w Grid width in threads (pixels). @param h Grid height in threads.
    /// @complexity O(1) host-side + the kernel's device work.
    /// @alloc none steady-state (first use of a kernel builds + caches its pipeline).
    /// @gpualloc none (buffers are caller-owned).
    /// @test plot:gpu_raster
    void dispatch_2d(const char* name, buffer_t** buffers, unsigned count, std::uint64_t w,
                     std::uint64_t h) {
        namespace d = cheatah::gpu::dispatch;
        if (count > kMaxBindings)
            throw std::runtime_error("cheatah-plot vulkan: too many buffer bindings");
        const std::uint32_t gx = d::group_count_1d(static_cast<std::uint32_t>(w), kLocal2d);
        const std::uint32_t gy = d::group_count_1d(static_cast<std::uint32_t>(h), kLocal2d);
        if (gx == 0u || gy == 0u) return;   // an empty grid is a no-op, not an error
        if (gx > limits_.maxComputeWorkGroupCount[0] || gy > limits_.maxComputeWorkGroupCount[1])
            throw std::runtime_error(std::string("cheatah-plot vulkan: dispatch of '") + name +
                                     "' exceeds maxComputeWorkGroupCount");
        VkPipeline pipe = pipeline(name);

        VkDescriptorBufferInfo infos[kMaxBindings]{};
        VkWriteDescriptorSet writes[kMaxBindings]{};
        for (unsigned i = 0; i < count; ++i) {
            infos[i].buffer = buffers[i]->buf;
            infos[i].range = VK_WHOLE_SIZE;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set_;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkc::UpdateDescriptorSets(device_, count, writes, 0, nullptr);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkc::BeginCommandBuffer(cmd_, &bi), "BeginCommandBuffer");
        vkc::CmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkc::CmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout_, 0, 1,
                                   &set_, 0, nullptr);
        vkc::CmdDispatch(cmd_, gx, gy, 1);
        check(vkc::EndCommandBuffer(cmd_), "EndCommandBuffer");

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd_;
        check(vkc::QueueSubmit(queue_, 1, &si, fence_), "QueueSubmit");
        check(vkc::WaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "WaitForFences");
        check(vkc::ResetFences(device_, 1, &fence_), "ResetFences");
        check(vkc::ResetCommandBuffer(cmd_, 0), "ResetCommandBuffer");
    }

private:
    /// Throw a tagged runtime_error when a Vulkan call fails. @complexity O(1). @alloc the
    /// error string on failure.
    static void check(VkResult r, const char* what) {
        if (r != VK_SUCCESS)
            throw std::runtime_error(std::string("cheatah-plot vulkan: ") + what + " failed (" +
                                     std::to_string(static_cast<int>(r)) + ")");
    }

    /// Create the instance (API 1.3, no layers/extensions — offscreen compute only).
    void create_instance() {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "cheatah-plot";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        check(vkc::CreateInstance(&ci, nullptr, &instance_), "CreateInstance");
    }

    /// Highest-scoring device with a compute queue (discrete > integrated > virtual > CPU),
    /// unless `CHEATAH_PLOT_VK_DEVICE` forces one by name substring or zero-based index.
    void pick_physical_device() {
        std::uint32_t n = 0;
        check(vkc::EnumeratePhysicalDevices(instance_, &n, nullptr), "EnumeratePhysicalDevices");
        if (n == 0) throw std::runtime_error("cheatah-plot vulkan: no devices");
        std::vector<VkPhysicalDevice> devs(n);
        check(vkc::EnumeratePhysicalDevices(instance_, &n, devs.data()),
              "EnumeratePhysicalDevices");

        const char* want = std::getenv("CHEATAH_PLOT_VK_DEVICE");
        int best_score = -1;
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t family = compute_family(devs[i]);
            if (family == UINT32_MAX) continue;
            VkPhysicalDeviceProperties props{};
            vkc::GetPhysicalDeviceProperties(devs[i], &props);
            if (want && *want) {
                // Forced selection: a zero-based index, or a deviceName substring.
                char* end = nullptr;
                const long idx = std::strtol(want, &end, 10);
                const bool by_index = end && *end == '\0';
                if (by_index ? (static_cast<long>(i) == idx)
                             : (std::string(props.deviceName).find(want) != std::string::npos)) {
                    select(devs[i], family, props);
                    return;
                }
                continue;
            }
            int score = 0;
            switch (props.deviceType) {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 4; break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 3; break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 2; break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 1; break;
                default:                                     score = 0; break;
            }
            if (score > best_score) {
                best_score = score;
                select(devs[i], family, props);
            }
        }
        if (physical_ == VK_NULL_HANDLE)
            throw std::runtime_error(
                want ? std::string("cheatah-plot vulkan: no device matches "
                                   "CHEATAH_PLOT_VK_DEVICE='") + want + "'"
                     : std::string("cheatah-plot vulkan: no device with a compute queue"));
    }

    /// Record the chosen physical device + queue family + its properties.
    void select(VkPhysicalDevice d, std::uint32_t family, const VkPhysicalDeviceProperties& p) {
        physical_ = d;
        family_ = family;
        device_name_ = p.deviceName;
        limits_ = p.limits;
    }

    /// The first queue family with compute, or UINT32_MAX when the device has none.
    [[nodiscard]] static std::uint32_t compute_family(VkPhysicalDevice d) {
        std::uint32_t n = 0;
        vkc::GetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
        std::vector<VkQueueFamilyProperties> fams(n);
        vkc::GetPhysicalDeviceQueueFamilyProperties(d, &n, fams.data());
        for (std::uint32_t i = 0; i < n; ++i)
            if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) return i;
        return UINT32_MAX;
    }

    /// Create the logical device + its one compute queue (no optional features needed).
    void create_device() {
        const float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family_;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        VkDeviceCreateInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        check(vkc::CreateDevice(physical_, &di, nullptr, &device_), "CreateDevice");
        vkc::GetDeviceQueue(device_, family_, 0, &queue_);
        vkc::GetPhysicalDeviceMemoryProperties(physical_, &memprops_);
    }

    /// One layout serves both kernels: kMaxBindings storage-buffer slots, compute stage; ONE
    /// persistent descriptor set, rewritten per (serial, blocking) dispatch.
    void create_descriptor_machinery() {
        VkDescriptorSetLayoutBinding bindings[kMaxBindings]{};
        for (std::uint32_t i = 0; i < kMaxBindings; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = kMaxBindings;
        li.pBindings = bindings;
        check(vkc::CreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_),
              "CreateDescriptorSetLayout");

        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &set_layout_;
        check(vkc::CreatePipelineLayout(device_, &pli, nullptr, &pipe_layout_),
              "CreatePipelineLayout");

        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        size.descriptorCount = kMaxBindings;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &size;
        check(vkc::CreateDescriptorPool(device_, &pi, nullptr, &pool_), "CreateDescriptorPool");

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pool_;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &set_layout_;
        check(vkc::AllocateDescriptorSets(device_, &dsai, &set_), "AllocateDescriptorSets");
    }

    /// One resettable command buffer + fence — every dispatch records, submits and blocks.
    void create_command_machinery() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = family_;
        check(vkc::CreateCommandPool(device_, &ci, nullptr, &cmd_pool_), "CreateCommandPool");
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmd_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        check(vkc::AllocateCommandBuffers(device_, &ai, &cmd_), "AllocateCommandBuffers");
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkc::CreateFence(device_, &fi, nullptr, &fence_), "CreateFence");
    }

    /// The index of a host-visible, host-coherent memory type for @p type_bits.
    [[nodiscard]] std::uint32_t host_memory_type(std::uint32_t type_bits) const {
        const VkMemoryPropertyFlags want =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (std::uint32_t i = 0; i < memprops_.memoryTypeCount; ++i)
            if ((type_bits & (1u << i)) &&
                (memprops_.memoryTypes[i].propertyFlags & want) == want)
                return i;
        throw std::runtime_error("cheatah-plot vulkan: no host-visible coherent memory");
    }

    /// The SPIR-V bytes for kernel `name`: `<spv dir>/<name>.spv`, where the directory is the
    /// `CHEATAH_PLOT_SPV_DIR` environment variable or, failing that, the same-named compile
    /// definition CMake bakes in (the build's shader directory).
    [[nodiscard]] static std::vector<char> spv_bytes(const std::string& name) {
        const char* env = std::getenv("CHEATAH_PLOT_SPV_DIR");
#if defined(CHEATAH_PLOT_SPV_DIR)
        const std::string dir = env ? env : CHEATAH_PLOT_SPV_DIR;
#else
        if (!env)
            throw std::runtime_error("cheatah-plot vulkan: CHEATAH_PLOT_SPV_DIR is neither "
                                     "defined nor in the environment");
        const std::string dir = env;
#endif
        std::ifstream in(dir + "/" + name + ".spv", std::ios::binary | std::ios::ate);
        if (!in)
            throw std::runtime_error("cheatah-plot vulkan: missing SPIR-V for kernel '" + name +
                                     "' under '" + dir + "'");
        std::vector<char> bytes(static_cast<std::size_t>(in.tellg()));
        in.seekg(0);
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    /// The compute pipeline for `name`, built from its .spv once and cached. slangc renames
    /// each module's single entry point to "main".
    VkPipeline pipeline(const std::string& name) {
        if (auto it = pipelines_.find(name); it != pipelines_.end()) return it->second;
        const std::vector<char> code = spv_bytes(name);
        VkShaderModuleCreateInfo mi{};
        mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mi.codeSize = code.size();
        mi.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        check(vkc::CreateShaderModule(device_, &mi, nullptr, &mod), "CreateShaderModule");

        VkComputePipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pi.stage.module = mod;
        pi.stage.pName = "main";
        pi.layout = pipe_layout_;
        VkPipeline pipe = VK_NULL_HANDLE;
        check(vkc::CreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe),
              "CreateComputePipelines");
        vkc::DestroyShaderModule(device_, mod, nullptr);
        pipelines_.emplace(name, pipe);
        return pipe;
    }

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memprops_{};
    std::uint32_t family_ = UINT32_MAX;
    std::string device_name_;
    VkPhysicalDeviceLimits limits_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;   // the one persistent set (serial dispatches)
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> pipelines_;
};

#endif  // CHEATAH_PLOT_GPU_VULKAN

#if defined(CHEATAH_PLOT_GPU_METAL)

/// Short alias for the cheatah-gpu Metal surface.
namespace mtl = cheatah::gpu::metal;

/**
 * Process-wide Metal context for the plot kernels: device + queue + a per-kernel pipeline
 * cache, with shared-storage buffers (unified memory — host-addressable via `contents()`).
 * On Apple the runtime compiles the slang-generated MSL; off Apple cheatah-gpu's
 * software-emulated Metal device runs the registered raster stand-ins — the SAME call sequence
 * either way, so the kernel seam is exercised end to end on a Linux host too. Constructed
 * lazily on first use via @ref ctx_of; lives for the process.
 */
class MetalContext {
public:
    /// The backend-native buffer handle type the context contract deals in.
    using buffer_t = mtl::Buffer;

    /// Bring the lane up: register the emulated stand-ins (off Apple) BEFORE any pipeline is
    /// built for them, then create the device + queue. Never throws — a failed bring-up leaves
    /// @ref ok false, which the shared probe reports as "no GPU".
    /// @complexity O(1). @alloc the autorelease pool + queue. @gpualloc none until a buffer.
    /// @test plot:gpu_raster
    MetalContext() {
#if !defined(__APPLE__)
        register_emulated_kernels();
#endif
        pool_ = mtl::AutoreleasePool::alloc()->init();
        dev_ = mtl::CreateSystemDefaultDevice();
        queue_ = dev_ ? dev_->newCommandQueue() : nullptr;
    }

    /// Release the pipelines, queue, device and pool. @complexity O(pipelines). @alloc none.
    /// @gpualloc frees every context-owned device object.
    ~MetalContext() {
        for (auto& [name, pso] : pipelines_) pso->release();
        if (queue_) queue_->release();
        if (dev_) dev_->release();
        if (pool_) pool_->release();
    }
    MetalContext(const MetalContext&) = delete;             ///< process-wide: not copyable.
    MetalContext& operator=(const MetalContext&) = delete;  ///< process-wide: not assignable.

    /// Whether the device and queue came up. @complexity O(1). @alloc none.
    /// @test plot:gpu_raster
    [[nodiscard]] bool ok() const { return dev_ != nullptr && queue_ != nullptr; }

    /// The device's advertised name (the emulated device reports its own).
    /// @return The device name, or "" when bring-up failed. @complexity O(1). @alloc the string.
    /// @test plot:gpu_raster
    [[nodiscard]] std::string device_name() const {
        if (dev_ == nullptr || dev_->name() == nullptr) return std::string();
        return dev_->name()->utf8String();
    }

    /// A shared-storage buffer of ≥ @p bytes bytes (host-addressable unified memory).
    /// @param bytes The requested size (floored to @ref kMinBufferBytes).
    /// @return The new buffer (caller releases via @ref release_buffer).
    /// @complexity O(1). @alloc none host-side. @gpualloc one shared-storage allocation.
    /// @test plot:gpu_raster
    [[nodiscard]] buffer_t* new_buffer(std::size_t bytes) {
        if (!ok()) throw std::runtime_error("cheatah-plot metal: no device");
        return dev_->newBuffer(bytes < kMinBufferBytes ? kMinBufferBytes : bytes,
                               mtl::ResourceStorageModeShared);
    }

    /// Host → buffer copy (unified memory: a plain memcpy).
    /// @param dst The destination buffer. @param src The host source. @param bytes Bytes to copy.
    /// @complexity O(bytes). @alloc none. @gpualloc none.
    /// @test plot:gpu_raster
    void upload(buffer_t* dst, const void* src, std::size_t bytes) {
        if (bytes > static_cast<std::size_t>(dst->length()))
            throw std::runtime_error("cheatah-plot metal: upload exceeds buffer size");
        std::memcpy(dst->contents(), src, bytes);
    }

    /// Buffer → host copy (unified memory: a plain memcpy).
    /// @param src The source buffer. @param dst The host destination. @param bytes Bytes to copy.
    /// @complexity O(bytes). @alloc none. @gpualloc none.
    /// @test plot:gpu_raster
    void download(buffer_t* src, void* dst, std::size_t bytes) {
        if (bytes > static_cast<std::size_t>(src->length()))
            throw std::runtime_error("cheatah-plot metal: download exceeds buffer size");
        std::memcpy(dst, src->contents(), bytes);
    }

    /// Release a buffer (dispatches are blocking, so nothing is in flight).
    /// @param b The buffer to release. @complexity O(1). @alloc none.
    /// @gpualloc frees the device allocation.
    /// @test plot:gpu_raster
    void release_buffer(buffer_t* b) { b->release(); }

    /// Bind @p count buffers at indices 0..count-1 and run kernel @p name over a @p w × @p h
    /// thread grid (threadgroups of @ref kLocal2d², ragged edges handled by `dispatchThreads`),
    /// blocking until complete.
    /// @param name The kernel entry name (@ref kClearKernel / @ref kRasterKernel).
    /// @param buffers The buffers, in the kernel's binding order.
    /// @param count How many bindings.
    /// @param w Grid width in threads (pixels). @param h Grid height in threads.
    /// @complexity O(1) host-side + the kernel's work (on the emulator: the stand-in's loop).
    /// @alloc a transient autorelease pool per dispatch. @gpualloc none (buffers caller-owned).
    /// @test plot:gpu_raster
    void dispatch_2d(const char* name, buffer_t** buffers, unsigned count, std::uint64_t w,
                     std::uint64_t h) {
        if (w == 0u || h == 0u) return;   // an empty grid is a no-op, not an error
        mtl::ComputePipelineState* pso = pipeline(name);
        mtl::AutoreleasePool* pool = mtl::AutoreleasePool::alloc()->init();
        mtl::CommandBuffer* cb = queue_->commandBuffer();
        mtl::ComputeCommandEncoder* enc = cb->computeCommandEncoder();
        enc->setComputePipelineState(pso);
        for (unsigned i = 0; i < count; ++i) enc->setBuffer(buffers[i], 0, i);
        enc->dispatchThreads(mtl::Size(w, h, 1),
                             mtl::Size(w < kLocal2d ? w : kLocal2d,
                                       h < kLocal2d ? h : kLocal2d, 1));
        enc->endEncoding();
        cb->commit();
        cb->waitUntilCompleted();
        pool->release();
    }

private:
    /// The MSL source for kernel `name`. On Apple: the slang-generated `<name>.metal` from the
    /// shader directory (env `CHEATAH_PLOT_MSL_DIR` beats the baked-in build path). Off Apple
    /// the emulator never reads source — a placeholder is enough to build the library.
    [[nodiscard]] static std::string msl_source(const std::string& name) {
#if defined(__APPLE__)
        const char* env = std::getenv("CHEATAH_PLOT_MSL_DIR");
#if defined(CHEATAH_PLOT_MSL_DIR)
        const std::string dir = env ? env : CHEATAH_PLOT_MSL_DIR;
#else
        if (!env)
            throw std::runtime_error("cheatah-plot metal: CHEATAH_PLOT_MSL_DIR is neither "
                                     "defined nor in the environment");
        const std::string dir = env;
#endif
        std::ifstream in(dir + "/" + name + ".metal");
        if (!in)
            throw std::runtime_error("cheatah-plot metal: missing MSL for kernel '" + name + "'");
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return src;
#else
        return "// emulated Metal device: source unused, kernel '" + name +
               "' runs the registered C++ stand-in\n";
#endif
    }

    /// The compute pipeline for `name`, built once and cached (each kernel gets its own small
    /// library; off Apple the emulator resolves the function name to the registered stand-in).
    mtl::ComputePipelineState* pipeline(const std::string& name) {
        if (auto it = pipelines_.find(name); it != pipelines_.end()) return it->second;
        const std::string source = msl_source(name);
        mtl::AutoreleasePool* pool = mtl::AutoreleasePool::alloc()->init();
        mtl::Error* err = nullptr;
        mtl::String* src = mtl::String::string(source.c_str(), mtl::UTF8StringEncoding);
        mtl::Library* lib =
            dev_->newLibrary(src, static_cast<const mtl::CompileOptions*>(nullptr), &err);
        if (!lib) {
            pool->release();
            throw std::runtime_error("cheatah-plot metal: MSL for kernel '" + name +
                                     "' did not compile");
        }
        mtl::String* fname = mtl::String::string(name.c_str(), mtl::UTF8StringEncoding);
        mtl::Function* fn = lib->newFunction(fname);
        mtl::ComputePipelineState* pso = dev_->newComputePipelineState(fn, &err);
        fn->release();
        lib->release();
        pool->release();
        if (!pso)
            throw std::runtime_error("cheatah-plot metal: no compute pipeline for kernel '" +
                                     name + "'");
        pipelines_.emplace(name, pso);
        return pso;
    }

    mtl::AutoreleasePool* pool_ = nullptr;
    mtl::Device* dev_ = nullptr;
    mtl::CommandQueue* queue_ = nullptr;
    std::unordered_map<std::string, mtl::ComputePipelineState*> pipelines_;
};

#endif  // CHEATAH_PLOT_GPU_METAL

}  // namespace detail

/**
 * The context contract the raster step drives — what BOTH lane classes implement. Every
 * template in the GPU path is constrained on this, so a third lane (or a test double) has a
 * compiler-checked surface to satisfy.
 */
template <class C>
concept RasterContext = requires(C& c, typename C::buffer_t* b, typename C::buffer_t** bufs,
                                 const void* src, void* dst, std::size_t bytes,
                                 const char* name, unsigned count, std::uint64_t w,
                                 std::uint64_t h) {
    { c.ok() } -> std::convertible_to<bool>;
    { c.new_buffer(bytes) } -> std::same_as<typename C::buffer_t*>;
    { c.upload(b, src, bytes) };
    { c.download(b, dst, bytes) };
    { c.release_buffer(b) };
    { c.dispatch_2d(name, bufs, count, w, h) };
};

namespace detail {

/// The one shared context of lane @p C — first call constructs the device, later calls reuse
/// it (a Meyers singleton per lane, so the dual-lane test matrix and the default lane share
/// bring-up). Throws what the lane's constructor throws.
/// @return The process-wide context of that lane.
/// @complexity O(1) after the first call.
/// @alloc the context on the first call. @gpualloc the lane's bring-up on the first call.
/// @test plot:gpu_raster
template <RasterContext C>
inline C& ctx_of() {
    static C c;
    return c;
}

// The platform-default lane `render()` drives: Metal on Apple, Vulkan elsewhere — cheatah-gpu's
// backend rule — degrading to whichever single lane the build has.
#if defined(__APPLE__) && defined(CHEATAH_PLOT_GPU_METAL)
using Context = MetalContext;    ///< the default lane on Apple.
#elif defined(CHEATAH_PLOT_GPU_VULKAN)
using Context = VulkanContext;   ///< the default lane off Apple.
#else
using Context = MetalContext;    ///< the only compiled lane.
#endif

}  // namespace detail

/**
 * Whether the default GPU lane can come up on this machine — a cached one-shot probe that NEVER
 * throws. This is the runtime "is there a GPU?" question: `render()` gates its GPU try on it
 * instead of hand-rolling a try/catch around a first dispatch.
 *
 * @return true when the default lane's context is (or can be) live; false when bring-up failed.
 * @complexity O(1) after the first call (the probe result is cached).
 * @alloc none after the first call. @gpualloc the lane's bring-up, once, on success.
 * @test plot:gpu_raster
 */
inline bool gpu_available() noexcept {
    static const bool ok = [] {
        try {
            return detail::ctx_of<detail::Context>().ok();
        } catch (...) {
            return false;
        }
    }();
    return ok;
}

}  // namespace cheatah::plot::renderer
