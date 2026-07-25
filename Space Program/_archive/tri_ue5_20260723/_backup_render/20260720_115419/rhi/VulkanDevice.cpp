// spr/rhi/vulkan/VulkanDevice.cpp
//
// LE backend Vulkan. Il implemente spr::IRenderDevice (Rhi.hpp). AUCUN type
// Vulkan ne fuit hors de ce fichier : la classe est confinee dans un namespace
// anonyme, seule la fabrique create_vulkan_device() est exposee. C'est ce qui
// rend l'API interchangeable — remplacer Vulkan = ecrire un autre .cpp de ce
// genre, sans toucher RenderCore/Scene/Camera/Hud.
//
// Portee de ce squelette (points d'extension notes // EXT:) :
//   instance -> surface Win32 -> device -> swapchain -> depth -> render pass ->
//   2 pipelines (maillages / lignes) -> buffers hote-visibles -> command buffers
//   -> synchronisation -> ImGui. Rend une scene reelle (planetes eclairees,
//   orbite, marqueur) issue du snapshot fige.
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "spr/rhi/Rhi.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef SPR_SHADER_DIR
#define SPR_SHADER_DIR "."
#endif

namespace spr {
namespace {

constexpr int    MAX_FRAMES = 2;
constexpr int    MAX_MATERIALS = 512;  // budget de sets (planetes + lunes + coquilles + anneaux + fond + modele interieur ISS)
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat HDR_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;  // cible de rendu HDR lineaire

// Push constants de la passe de composition (miroir de composite.frag).
struct PostPush {
  float exposure;
  float bloom_strength;
  float pad0;
  float pad1;
};
// Push constants des passes de bloom (miroir de bright.frag / blur.frag).
struct BloomPush {
  float dir_x;
  float dir_y;
  float threshold;
  float pad;
};

void vk_check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "Vulkan: %s a echoue (VkResult=%d)", what, (int)r);
    throw std::runtime_error(buf);
  }
}

// Doit correspondre a scene.vert / scene.frag / planet.* (std140-friendly).
struct PushConstants {
  Mat4 model;   // 64
  Vec4 color;   // 16
  int  style;   // 4
  int  pad[3];  // 12  -> 96 octets (<= 128, minimum garanti)
};
// Bloc Frame (set=0, binding=0). MIROIR de common.glsl : tout ecart d'offset
// decalerait l'UBO. static_assert plus bas verrouille la taille.
struct FrameUbo {
  Mat4 view;       // 0   (64)
  Mat4 proj;       // 64  (64)
  Vec4 sun;        // 128 xyz = Soleil (camera-relative), w = has_sun
  Vec4 sun_color;  // 144 rgb couleur, w intensite
  Vec4 ambient;    // 160 rgb fill ambiant, w exposure
};                 // 176
static_assert(sizeof(FrameUbo) == 176, "FrameUbo std140 layout");

// Bloc Material (set=1, binding=0). MIROIR du bloc Material de planet.frag.
struct MaterialUbo {
  Vec4  base_color;  // 0   rgb teinte, w alpha
  Vec4  pbr;         // 16  x rough, y metallic, z emissive, w nightIntensity
  Vec4  extra;       // 32  x rimStrength, y oceanLevel, z detailScale, w reserve
  Vec4  color_low;   // 48
  Vec4  color_mid;   // 64
  Vec4  color_high;  // 80
  std::int32_t flags[4]; // 96  x archetype, y features, z seed, w reserve
};                   // 112
static_assert(sizeof(MaterialUbo) == 112, "MaterialUbo std140 layout");

struct MeshEntry {
  VkBuffer       vbuf{VK_NULL_HANDLE};
  VkDeviceMemory vmem{VK_NULL_HANDLE};
  void*          vmapped{nullptr};
  std::uint32_t  vcount{0};
  std::uint32_t  vcap{0};
  VkBuffer       ibuf{VK_NULL_HANDLE};
  VkDeviceMemory imem{VK_NULL_HANDLE};
  std::uint32_t  icount{0};
  VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
};

struct TextureEntry {
  VkImage        image{VK_NULL_HANDLE};
  VkDeviceMemory mem{VK_NULL_HANDLE};
  VkImageView    view{VK_NULL_HANDLE};
};

struct MaterialEntry {
  VkBuffer        ubo{VK_NULL_HANDLE};
  VkDeviceMemory  ubo_mem{VK_NULL_HANDLE};
  void*           ubo_mapped{nullptr};
  VkDescriptorSet set{VK_NULL_HANDLE};
};

std::vector<char> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f) throw std::runtime_error("Shader introuvable: " + path);
  const size_t n = static_cast<size_t>(f.tellg());
  std::vector<char> data(n);
  f.seekg(0);
  f.read(data.data(), static_cast<std::streamsize>(n));
  return data;
}

class VulkanDevice final : public IRenderDevice {
 public:
  explicit VulkanDevice(const DeviceConfig& cfg) : cfg_(cfg) {
    create_instance();
    create_surface();
    pick_physical_device();
    pick_msaa();                        // niveau de multisampling supporte (bords lisses)
    create_logical_device();
    create_swapchain();
    create_depth();                     // par-frame (reversed-Z), multisample si MSAA
    create_hdr_targets();               // cibles HDR par-frame (R16F, resolve MSAA)
    create_msaa_targets();              // cible couleur multisample par-frame
    create_render_pass();               // passe scene (HDR) + passe present (swapchain)
    create_framebuffers();              // fb scene (HDR+depth) + fb present (swapchain)
    create_descriptors_and_ubo();       // set = 0 (frame)
    create_material_system();           // set = 1 (materiau) : layout + pool + sampler
    create_pipelines();                 // a besoin des layouts set0 + set1
    create_commands();
    create_default_textures();          // a besoin de cmd_pool_ + sampler_
    create_post();                      // composition HDR->LDR (a besoin des cibles + defauts)
    create_bloom_pipelines();           // passe/pipelines bloom (independant de la taille)
    create_bloom_targets();             // cibles bloom half-res + descripteurs
    create_sync();
  }

  ~VulkanDevice() override {
    if (device_) vkDeviceWaitIdle(device_);
    destroy_imgui();
    for (auto& kv : materials_) free_material(kv.second);
    materials_.clear();
    for (auto& kv : textures_) free_texture(kv.second);
    textures_.clear();
    for (auto& kv : meshes_) free_mesh(kv.second);
    meshes_.clear();
    destroy_material_system();
    destroy_bloom_targets();
    destroy_bloom_pipelines();
    destroy_post();
    if (readback_buf_) vkDestroyBuffer(device_, readback_buf_, nullptr);
    if (readback_mem_) vkFreeMemory(device_, readback_mem_, nullptr);
    destroy_sync();
    if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    destroy_pipelines();
    destroy_descriptors_and_ubo();
    destroy_framebuffers();
    destroy_hdr_targets();
    destroy_msaa_targets();
    destroy_depth();
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (rp_post_) vkDestroyRenderPass(device_, rp_post_, nullptr);
    destroy_swapchain();
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_messenger_) {
      auto f = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance_, "vkDestroyDebugUtilsMessengerEXT");
      if (f) f(instance_, debug_messenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
  }

  const char* device_name() const override { return device_name_.c_str(); }

  // ---- ressources -----------------------------------------------------------
  MeshHandle create_mesh(const MeshDesc& d) override {
    MeshEntry e{};
    // retro-compat : `lines` prime, sinon la topologie explicite.
    if (d.lines)                                    e.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    else if (d.topology == Topology::LineStrip)     e.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    else if (d.topology == Topology::PointList)     e.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    else                                            e.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    e.vcap = std::max(d.max_vertices, d.vertex_count);
    if (e.vcap == 0) e.vcap = 1;
    create_host_buffer(e.vcap * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       e.vbuf, e.vmem, &e.vmapped);
    if (d.vertices && d.vertex_count) {
      std::memcpy(e.vmapped, d.vertices, d.vertex_count * sizeof(Vertex));
      e.vcount = d.vertex_count;
    }
    if (d.indices && d.index_count) {
      void* im = nullptr;
      create_host_buffer(d.index_count * sizeof(std::uint32_t),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT, e.ibuf, e.imem, &im);
      std::memcpy(im, d.indices, d.index_count * sizeof(std::uint32_t));
      e.icount = d.index_count;
    }
    const MeshHandle h = next_handle_++;
    meshes_.emplace(h, e);
    return h;
  }

  void update_vertices(MeshHandle h, const Vertex* v, std::uint32_t count) override {
    auto it = meshes_.find(h);
    if (it == meshes_.end()) return;
    MeshEntry& e = it->second;
    count = std::min(count, e.vcap);
    std::memcpy(e.vmapped, v, count * sizeof(Vertex));
    e.vcount = count;
  }

  void destroy_mesh(MeshHandle h) override {
    auto it = meshes_.find(h);
    if (it == meshes_.end()) return;
    vkDeviceWaitIdle(device_);
    free_mesh(it->second);
    meshes_.erase(it);
  }

  // ---- textures & materiaux -------------------------------------------------
  TextureHandle create_texture(const TextureDesc& d) override {
    const VkFormat fmt = d.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const std::uint32_t w = std::max(1u, d.width), h = std::max(1u, d.height);
    static const std::uint8_t kWhite[4] = {255, 255, 255, 255};
    const std::uint8_t* px = d.rgba ? d.rgba : kWhite;
    TextureEntry e = create_texture_internal(px, w, h, fmt);
    const TextureHandle handle = next_tex_++;
    textures_.emplace(handle, e);
    return handle;
  }

  void destroy_texture(TextureHandle h) override {
    auto it = textures_.find(h);
    if (it == textures_.end()) return;
    vkDeviceWaitIdle(device_);
    free_texture(it->second);
    textures_.erase(it);
  }

  MaterialHandle create_material(const MaterialDesc& d) override {
    MaterialEntry e{};
    create_host_buffer(sizeof(MaterialUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       e.ubo, e.ubo_mem, &e.ubo_mapped);
    pack_material(d.params, *static_cast<MaterialUbo*>(e.ubo_mapped));

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = mat_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &mat_layout_;
    vk_check(vkAllocateDescriptorSets(device_, &ai, &e.set),
             "vkAllocateDescriptorSets(material)");
    write_material_set(e, d);

    const MaterialHandle handle = next_mat_++;
    materials_.emplace(handle, e);
    return handle;
  }

  void update_material_params(MaterialHandle h, const MaterialParams& p) override {
    auto it = materials_.find(h);
    if (it == materials_.end()) return;
    pack_material(p, *static_cast<MaterialUbo*>(it->second.ubo_mapped));
  }

  void destroy_material(MaterialHandle h) override {
    auto it = materials_.find(h);
    if (it == materials_.end()) return;
    vkDeviceWaitIdle(device_);
    free_material(it->second);
    materials_.erase(it);
  }

  // ---- cycle de frame -------------------------------------------------------
  bool begin_frame() override {
    vkWaitForFences(device_, 1, &in_flight_[frame_], VK_TRUE, UINT64_MAX);

    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                         img_available_[frame_], VK_NULL_HANDLE,
                                         &image_index_);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { recreate_swapchain(); return false; }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
      vk_check(acq, "vkAcquireNextImageKHR");

    if (images_in_flight_[image_index_] != VK_NULL_HANDLE)
      vkWaitForFences(device_, 1, &images_in_flight_[image_index_], VK_TRUE, UINT64_MAX);
    images_in_flight_[image_index_] = in_flight_[frame_];
    vkResetFences(device_, 1, &in_flight_[frame_]);

    VkCommandBuffer cmd = cmd_bufs_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vk_check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");

    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};      // noir HDR (l'espace ; le fond vient du starfield)
    clears[1].depthStencil = {0.0f, 0};                // reversed-Z : le "loin" est 0
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass_;              // passe SCENE (cible HDR)
    rp.framebuffer = fb_scene_[frame_];        // par-frame (pas d'aliasing entre frames)
    rp.renderArea.extent = extent_;
    rp.clearValueCount = static_cast<std::uint32_t>(clears.size());
    rp.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = extent_;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    return true;
  }

  void submit(const DrawList& dl) override {
    // UBO de la frame (host-coherent : le fence garantit que la frame precedente
    // qui l'utilisait est terminee).
    FrameUbo ubo{};
    ubo.view = dl.frame.view;
    ubo.proj = dl.frame.proj;
    ubo.sun = Vec4{dl.frame.sun_render, dl.frame.has_sun ? 1.0f : 0.0f};
    ubo.sun_color = Vec4{dl.frame.sun_color, dl.frame.sun_intensity};
    ubo.ambient = Vec4{dl.frame.ambient, dl.frame.exposure};
    std::memcpy(ubo_mapped_[frame_], &ubo, sizeof ubo);

    VkCommandBuffer cmd = cmd_bufs_[frame_];
    for (std::uint32_t i = 0; i < dl.count; ++i) {
      const DrawItem& it = dl.items[i];
      auto mit = meshes_.find(it.mesh);
      if (mit == meshes_.end()) continue;
      const MeshEntry& e = mit->second;

      // Selection du pipeline : points -> starfield, lignes -> pipe_line_, sinon
      // triangles (materiau planetaire si style PlanetPbr + materiau valide).
      VkPipeline pipe = pipe_mesh_;
      VkPipelineLayout layout = pipe_layout_;
      const MaterialEntry* mat = nullptr;
      if (e.topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) {
        pipe = pipe_star_;
      } else if (e.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        pipe = pipe_line_;
      } else if ((it.style == DrawStyle::PlanetPbr || it.style == DrawStyle::Shell ||
                  it.style == DrawStyle::Ring || it.style == DrawStyle::MeshTextured) &&
                 it.material != INVALID_MATERIAL) {
        auto matit = materials_.find(it.material);
        if (matit != materials_.end()) {
          pipe = (it.style == DrawStyle::Shell)        ? pipe_shell_
               : (it.style == DrawStyle::Ring)         ? pipe_ring_
               : (it.style == DrawStyle::MeshTextured) ? pipe_mesh_tex_
               :                                         pipe_planet_;
          layout = pipe_layout_planet_;
          mat = &matit->second;
        }
      }

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      // Lignes : largeur dynamique (survol -> trajectoire epaissie), bornee a la
      // plage supportee (1.0 si wideLines absent).
      if (e.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        float lw = it.line_width;
        if (lw < 1.0f) lw = 1.0f;
        if (lw > line_width_max_) lw = line_width_max_;
        vkCmdSetLineWidth(cmd, lw);
      }
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                              &desc_sets_[frame_], 0, nullptr);   // set 0 : frame
      if (mat)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout_planet_,
                                1, 1, &mat->set, 0, nullptr);     // set 1 : materiau

      PushConstants pc{};
      pc.model = it.model;
      pc.color = it.color;
      pc.style = static_cast<int>(it.style);
      vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof pc, &pc);

      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &e.vbuf, &off);
      if (e.icount > 0) {
        vkCmdBindIndexBuffer(cmd, e.ibuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, e.icount, 1, 0, 0, 0);
      } else if (e.vcount > 0) {
        vkCmdDraw(cmd, e.vcount, 1, 0, 0);
      }
    }
  }

  void draw_hud() override {
    // On finalise seulement les donnees ImGui ici : leur ENREGISTREMENT se fait
    // dans la passe de PRESENT (apres tonemapping) pour que l'UI ne soit pas
    // tonemappee. Voir end_frame().
    ImGui::Render();
    hud_pending_ = imgui_ready_;
  }

  void end_frame() override {
    VkCommandBuffer cmd = cmd_bufs_[frame_];
    vkCmdEndRenderPass(cmd);   // fin passe SCENE : hdr_[frame_] est en lecture shader

    // Bloom (Phase 3) : rempli les cibles de bloom depuis hdr_[frame_].
    record_bloom(cmd);

    // --- passe PRESENT : composition HDR->LDR (tonemap + exposition + bloom) + UI
    VkClearValue pclear{}; pclear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo prp{};
    prp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    prp.renderPass = rp_post_;
    prp.framebuffer = framebuffers_[image_index_];
    prp.renderArea.extent = extent_;
    prp.clearValueCount = 1;
    prp.pClearValues = &pclear;
    vkCmdBeginRenderPass(cmd, &prp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scr{}; scr.extent = extent_;
    vkCmdSetScissor(cmd, 0, 1, &scr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_composite_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipe_layout_, 0, 1,
                            &post_set_[frame_], 0, nullptr);
    PostPush pp{};
    pp.exposure = exposure_;
    pp.bloom_strength = bloom_strength_;
    vkCmdPushConstants(cmd, post_pipe_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pp, &pp);
    vkCmdDraw(cmd, 3, 1, 0, 0);   // triangle plein ecran

    if (hud_pending_) {
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);  // UI en LDR, non tonemappee
      hud_pending_ = false;
    }
    vkCmdEndRenderPass(cmd);   // fin passe PRESENT : swapchain en PRESENT_SRC

    const bool do_capture = !capture_path_.empty();
    if (do_capture) record_capture_copy(cmd);  // image en PRESENT_SRC -> copie readback
    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkSemaphore wait = img_available_[frame_];
    VkSemaphore signal = render_finished_[image_index_];
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &signal;
    vk_check(vkQueueSubmit(queue_, 1, &si, in_flight_[frame_]), "vkQueueSubmit");

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &signal;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_index_;
    VkResult pr = vkQueuePresentKHR(queue_, &pi);

    if (do_capture) {
      vkQueueWaitIdle(queue_);   // garantit que la copie readback est terminee
      write_capture_bmp(capture_path_.c_str());
      capture_path_.clear();
    }

    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || resized_)
      recreate_swapchain();
    else if (pr != VK_SUCCESS)
      vk_check(pr, "vkQueuePresentKHR");

    frame_ = (frame_ + 1) % MAX_FRAMES;
  }

  void resize(std::uint32_t w, std::uint32_t h) override {
    if (w == 0 || h == 0) return;
    resized_ = true;  // pris en compte au prochain present / recreate
  }

  void wait_idle() override { if (device_) vkDeviceWaitIdle(device_); }

  void request_capture(const char* ppm_path) override {
    if (capture_supported_ && ppm_path) capture_path_ = ppm_path;
  }

  // ---- ImGui ----------------------------------------------------------------
  void imgui_init(void* glfw_window) override {
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(device_, &pci, nullptr, &imgui_pool_),
             "vkCreateDescriptorPool(imgui)");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // pas de imgui.ini
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(glfw_window), true);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = instance_;
    ii.PhysicalDevice = phys_;
    ii.Device = device_;
    ii.QueueFamily = queue_family_;
    ii.Queue = queue_;
    ii.DescriptorPool = imgui_pool_;
    ii.RenderPass = rp_post_;            // l'UI est dessinee en passe de present (LDR)
    ii.MinImageCount = min_image_count_;
    ii.ImageCount = image_count_;
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.Subpass = 0;
    ImGui_ImplVulkan_Init(&ii);
    // Les fonts sont uploadees automatiquement au premier NewFrame() (ImGui 1.90).
    imgui_ready_ = true;
  }

  void imgui_new_frame() override {
    if (!imgui_ready_) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void imgui_shutdown() override { destroy_imgui(); }

 private:
  // =========================== creation ====================================
  void create_instance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Space Program";
    app.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME,
                                     VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    std::vector<const char*> layers;
    if (cfg_.enable_validation && validation_available()) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    vk_check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");

    if (!layers.empty()) setup_debug_messenger();
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
      VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
      const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
      std::fprintf(stderr, "[vk] %s\n", data->pMessage);
    return VK_FALSE;
  }

  void setup_debug_messenger() {
    auto f = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    if (!f) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = &VulkanDevice::debug_cb;
    f(instance_, &ci, nullptr, &debug_messenger_);
  }

  bool validation_available() {
    std::uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> props(n);
    vkEnumerateInstanceLayerProperties(&n, props.data());
    for (auto& p : props)
      if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) return true;
    return false;
  }

  void create_surface() {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = static_cast<HINSTANCE>(cfg_.hinstance);
    ci.hwnd = static_cast<HWND>(cfg_.hwnd);
    vk_check(vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_),
             "vkCreateWin32SurfaceKHR");
  }

  void pick_physical_device() {
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) throw std::runtime_error("Aucun GPU Vulkan");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());

    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (auto d : devs) {
      int qf = find_graphics_present_family(d);
      if (qf < 0 || !has_swapchain_ext(d)) continue;
      if (fallback == VK_NULL_HANDLE) fallback = d;
      VkPhysicalDeviceProperties p{};
      vkGetPhysicalDeviceProperties(d, &p);
      if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        phys_ = d;
        queue_family_ = static_cast<std::uint32_t>(qf);
        break;
      }
    }
    if (phys_ == VK_NULL_HANDLE) {
      if (fallback == VK_NULL_HANDLE)
        throw std::runtime_error("Aucun GPU avec graphics+present+swapchain");
      phys_ = fallback;
      queue_family_ = static_cast<std::uint32_t>(find_graphics_present_family(fallback));
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_, &props);
    device_name_ = props.deviceName;
  }

  int find_graphics_present_family(VkPhysicalDevice d) {
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fam(n);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, fam.data());
    for (std::uint32_t i = 0; i < n; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
      if ((fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
        return static_cast<int>(i);
    }
    return -1;
  }

  bool has_swapchain_ext(VkPhysicalDevice d) {
    std::uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    vkEnumerateDeviceExtensionProperties(d, nullptr, &n, ext.data());
    for (auto& e : ext)
      if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) return true;
    return false;
  }

  void create_logical_device() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = queue_family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;

    // Features : largePoints pour dimensionner les etoiles (gl_PointSize > 1),
    // wideLines pour d'eventuelles lignes epaisses. Actives seulement si le GPU
    // les supporte -> aucune exigence dure (fallback = points/lignes de 1 px).
    VkPhysicalDeviceFeatures avail{};
    vkGetPhysicalDeviceFeatures(phys_, &avail);
    VkPhysicalDeviceFeatures feats{};
    if (avail.largePoints) feats.largePoints = VK_TRUE;
    if (avail.wideLines)   feats.wideLines = VK_TRUE;
    // Plage de largeur de trait supportee (survol : trajectoire epaissie). Sans
    // wideLines, seule 1.0 est valide -> on borne le max a 1.0 (degradation propre).
    wide_lines_ = (avail.wideLines == VK_TRUE);
    {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(phys_, &props);
      line_width_max_ = wide_lines_ ? props.limits.lineWidthRange[1] : 1.0f;
      if (line_width_max_ < 1.0f) line_width_max_ = 1.0f;
    }

    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = dext;
    ci.pEnabledFeatures = &feats;
    vk_check(vkCreateDevice(phys_, &ci, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  }

  VkSurfaceFormatKHR choose_format() {
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> f(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, f.data());
    for (auto& s : f)
      if (s.format == VK_FORMAT_B8G8R8A8_UNORM &&
          s.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return s;
    return f[0];
  }

  void create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps);

    VkSurfaceFormatKHR fmt = choose_format();
    swap_format_ = fmt.format;

    if (caps.currentExtent.width != UINT32_MAX) {
      extent_ = caps.currentExtent;
    } else {
      extent_.width = std::clamp(cfg_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
      extent_.height = std::clamp(cfg_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    min_image_count_ = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && min_image_count_ > caps.maxImageCount)
      min_image_count_ = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = min_image_count_;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    // TRANSFER_SRC (si supporte) autorise le readback swapchain -> capture PPM.
    capture_supported_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    (capture_supported_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // garanti, vsync
    ci.clipped = VK_TRUE;
    vk_check(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_),
             "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count_, nullptr);
    images_.resize(image_count_);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count_, images_.data());

    image_views_.resize(image_count_);
    for (std::uint32_t i = 0; i < image_count_; ++i)
      image_views_[i] = make_view(images_[i], swap_format_, VK_IMAGE_ASPECT_COLOR_BIT);

    images_in_flight_.assign(image_count_, VK_NULL_HANDLE);
  }

  VkImageView make_view(VkImage img, VkFormat fmt, VkImageAspectFlags aspect,
                        std::uint32_t levels = 1) {
    VkImageViewCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image = img;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = fmt;
    ci.subresourceRange.aspectMask = aspect;
    ci.subresourceRange.levelCount = levels;
    ci.subresourceRange.layerCount = 1;
    VkImageView v{};
    vk_check(vkCreateImageView(device_, &ci, nullptr, &v), "vkCreateImageView");
    return v;
  }

  void pick_msaa() {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(phys_, &p);
    const VkSampleCountFlags c =
        p.limits.framebufferColorSampleCounts & p.limits.framebufferDepthSampleCounts;
    if (c & VK_SAMPLE_COUNT_4_BIT)      msaa_ = VK_SAMPLE_COUNT_4_BIT;   // 4x : bon compromis
    else if (c & VK_SAMPLE_COUNT_2_BIT) msaa_ = VK_SAMPLE_COUNT_2_BIT;
    else                                msaa_ = VK_SAMPLE_COUNT_1_BIT;
  }

  // Cible couleur multisample par-frame (resolue vers hdr_[i] par la passe scene).
  void create_msaa_targets() {
    if (msaa_ == VK_SAMPLE_COUNT_1_BIT) return;
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkImageCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      ci.imageType = VK_IMAGE_TYPE_2D;
      ci.format = HDR_FORMAT;
      ci.extent = {extent_.width, extent_.height, 1};
      ci.mipLevels = 1;
      ci.arrayLayers = 1;
      ci.samples = msaa_;
      ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;   // resolu -> jamais echantillonne
      ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vk_check(vkCreateImage(device_, &ci, nullptr, &msaa_img_[i]), "vkCreateImage(msaa)");
      VkMemoryRequirements req{};
      vkGetImageMemoryRequirements(device_, msaa_img_[i], &req);
      VkMemoryAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vk_check(vkAllocateMemory(device_, &ai, nullptr, &msaa_mem_[i]), "vkAllocateMemory(msaa)");
      vkBindImageMemory(device_, msaa_img_[i], msaa_mem_[i], 0);
      msaa_view_[i] = make_view(msaa_img_[i], HDR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
  }
  void destroy_msaa_targets() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      if (msaa_view_[i]) vkDestroyImageView(device_, msaa_view_[i], nullptr);
      if (msaa_img_[i]) vkDestroyImage(device_, msaa_img_[i], nullptr);
      if (msaa_mem_[i]) vkFreeMemory(device_, msaa_mem_[i], nullptr);
      msaa_view_[i] = VK_NULL_HANDLE; msaa_img_[i] = VK_NULL_HANDLE; msaa_mem_[i] = VK_NULL_HANDLE;
    }
  }

  void create_depth() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkImageCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      ci.imageType = VK_IMAGE_TYPE_2D;
      ci.format = DEPTH_FORMAT;
      ci.extent = {extent_.width, extent_.height, 1};
      ci.mipLevels = 1;
      ci.arrayLayers = 1;
      ci.samples = msaa_;   // depth multisample (doit matcher la couleur scene)
      ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vk_check(vkCreateImage(device_, &ci, nullptr, &depth_img_[i]), "vkCreateImage(depth)");
      VkMemoryRequirements req{};
      vkGetImageMemoryRequirements(device_, depth_img_[i], &req);
      VkMemoryAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vk_check(vkAllocateMemory(device_, &ai, nullptr, &depth_mem_[i]), "vkAllocateMemory(depth)");
      vkBindImageMemory(device_, depth_img_[i], depth_mem_[i], 0);
      depth_view_[i] = make_view(depth_img_[i], DEPTH_FORMAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
  }

  void create_render_pass() {
    // --- passe SCENE : cible HDR (R16F) + depth. finalLayout HDR = lecture shader
    //     (la passe de composition l'echantillonne).
    const bool ms = (msaa_ != VK_SAMPLE_COUNT_1_BIT);

    VkAttachmentDescription color{};
    color.format = HDR_FORMAT;
    color.samples = msaa_;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // MSAA : la couleur multisample est RESOLUE vers hdr_ (attachment 2) puis jetee.
    color.storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = DEPTH_FORMAT;
    depth.samples = msaa_;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;   // depth non relu ensuite
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Cible RESOLUE (single-sample) = hdr_ : c'est elle que la composition echantillonne.
    VkAttachmentDescription resolve{};
    resolve.format = HDR_FORMAT;
    resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;
    if (ms) sub.pResolveAttachments = &resolveRef;

    // Entree : attend la disponibilite de l'image. Sortie : la couleur ecrite doit
    // etre visible pour l'echantillonnage (composition) -> dependance 0->EXTERNAL.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 3> att{color, depth, resolve};
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = ms ? 3u : 2u;   // resolve seulement en MSAA
    ci.pAttachments = att.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = static_cast<std::uint32_t>(deps.size());
    ci.pDependencies = deps.data();
    vk_check(vkCreateRenderPass(device_, &ci, nullptr, &render_pass_), "vkCreateRenderPass(scene)");

    // --- passe PRESENT : swapchain (UNORM), sans depth. Composition + ImGui.
    VkAttachmentDescription pcolor{};
    pcolor.format = swap_format_;
    pcolor.samples = VK_SAMPLE_COUNT_1_BIT;
    pcolor.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // la composition remplit tout l'ecran
    pcolor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    pcolor.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    pcolor.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    pcolor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pcolor.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference pcolorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription psub{};
    psub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    psub.colorAttachmentCount = 1;
    psub.pColorAttachments = &pcolorRef;
    VkSubpassDependency pdep{};
    pdep.srcSubpass = VK_SUBPASS_EXTERNAL;
    pdep.dstSubpass = 0;
    pdep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    pdep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    pdep.srcAccessMask = 0;
    pdep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pci.attachmentCount = 1;
    pci.pAttachments = &pcolor;
    pci.subpassCount = 1;
    pci.pSubpasses = &psub;
    pci.dependencyCount = 1;
    pci.pDependencies = &pdep;
    vk_check(vkCreateRenderPass(device_, &pci, nullptr, &rp_post_), "vkCreateRenderPass(post)");
  }

  // Cibles HDR par-frame (R16F) : 2 frames en vol -> 2 cibles pour eviter toute
  // course de donnees (la composition de la frame N lit hdr[N] pendant que la
  // frame N+1 ecrit hdr[N+1]).
  void create_hdr_targets() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkImageCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      ci.imageType = VK_IMAGE_TYPE_2D;
      ci.format = HDR_FORMAT;
      ci.extent = {extent_.width, extent_.height, 1};
      ci.mipLevels = 1;
      ci.arrayLayers = 1;
      ci.samples = VK_SAMPLE_COUNT_1_BIT;
      ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vk_check(vkCreateImage(device_, &ci, nullptr, &hdr_img_[i]), "vkCreateImage(hdr)");
      VkMemoryRequirements req{};
      vkGetImageMemoryRequirements(device_, hdr_img_[i], &req);
      VkMemoryAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vk_check(vkAllocateMemory(device_, &ai, nullptr, &hdr_mem_[i]), "vkAllocateMemory(hdr)");
      vkBindImageMemory(device_, hdr_img_[i], hdr_mem_[i], 0);
      hdr_view_[i] = make_view(hdr_img_[i], HDR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
  }
  void destroy_hdr_targets() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      if (hdr_view_[i]) vkDestroyImageView(device_, hdr_view_[i], nullptr);
      if (hdr_img_[i]) vkDestroyImage(device_, hdr_img_[i], nullptr);
      if (hdr_mem_[i]) vkFreeMemory(device_, hdr_mem_[i], nullptr);
      hdr_view_[i] = VK_NULL_HANDLE; hdr_img_[i] = VK_NULL_HANDLE; hdr_mem_[i] = VK_NULL_HANDLE;
    }
  }

  void create_framebuffers() {
    // Framebuffers de SCENE (par-frame). MSAA : [couleur MSAA, depth MSAA, resolve=HDR].
    // Sans MSAA : [HDR, depth]. L'ordre suit les attachments de render_pass_.
    const bool ms = (msaa_ != VK_SAMPLE_COUNT_1_BIT);
    for (int i = 0; i < MAX_FRAMES; ++i) {
      std::array<VkImageView, 3> att{};
      std::uint32_t n;
      if (ms) { att = {msaa_view_[i], depth_view_[i], hdr_view_[i]}; n = 3; }
      else    { att = {hdr_view_[i], depth_view_[i], VK_NULL_HANDLE}; n = 2; }
      VkFramebufferCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      ci.renderPass = render_pass_;
      ci.attachmentCount = n;
      ci.pAttachments = att.data();
      ci.width = extent_.width;
      ci.height = extent_.height;
      ci.layers = 1;
      vk_check(vkCreateFramebuffer(device_, &ci, nullptr, &fb_scene_[i]), "vkCreateFramebuffer(scene)");
    }
    // Framebuffers de PRESENT (par image de swapchain) : swapchain seul, passe rp_post_.
    framebuffers_.resize(image_count_);
    for (std::uint32_t i = 0; i < image_count_; ++i) {
      VkFramebufferCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      ci.renderPass = rp_post_;
      ci.attachmentCount = 1;
      ci.pAttachments = &image_views_[i];
      ci.width = extent_.width;
      ci.height = extent_.height;
      ci.layers = 1;
      vk_check(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]), "vkCreateFramebuffer(present)");
    }
  }

  void create_descriptors_and_ubo() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1;
    lci.pBindings = &b;
    vk_check(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &desc_layout_),
             "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = MAX_FRAMES;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    vk_check(vkCreateDescriptorPool(device_, &pci, nullptr, &desc_pool_),
             "vkCreateDescriptorPool");

    for (int i = 0; i < MAX_FRAMES; ++i) {
      create_host_buffer(sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         ubo_buf_[i], ubo_mem_[i], &ubo_mapped_[i]);
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = desc_pool_;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &desc_layout_;
      vk_check(vkAllocateDescriptorSets(device_, &ai, &desc_sets_[i]),
               "vkAllocateDescriptorSets");
      VkDescriptorBufferInfo bi{ubo_buf_[i], 0, sizeof(FrameUbo)};
      VkWriteDescriptorSet w{};
      w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w.dstSet = desc_sets_[i];
      w.dstBinding = 0;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.pBufferInfo = &bi;
      vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
  }

  VkShaderModule load_shader(const std::string& file) {
    auto code = read_file(std::string(SPR_SHADER_DIR) + "/" + file);
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
    VkShaderModule m{};
    vk_check(vkCreateShaderModule(device_, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
  }

  // Options de fabrication d'un pipeline (topologie, layout, depth, blend, cull).
  struct PipeOpts {
    VkPrimitiveTopology topo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineLayout    layout{VK_NULL_HANDLE};
    bool                depth_test{true};
    bool                depth_write{true};
    bool                additive{false};
    bool                alpha_blend{false};   // src_alpha / 1-src_alpha (trajectoires fondues)
    VkCullModeFlags     cull{VK_CULL_MODE_NONE};
  };

  void create_pipelines() {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(PushConstants);

    // layout de base : set 0 (frame) + push constants (legacy + starfield).
    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &desc_layout_;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pcr;
    vk_check(vkCreatePipelineLayout(device_, &lci, nullptr, &pipe_layout_),
             "vkCreatePipelineLayout");

    // layout planetaire : set 0 (frame) + set 1 (materiau) + push constants.
    VkDescriptorSetLayout sets[2] = {desc_layout_, mat_layout_};
    VkPipelineLayoutCreateInfo lci2 = lci;
    lci2.setLayoutCount = 2;
    lci2.pSetLayouts = sets;
    vk_check(vkCreatePipelineLayout(device_, &lci2, nullptr, &pipe_layout_planet_),
             "vkCreatePipelineLayout(planet)");

    // --- pipelines legacy (corps simples, lignes, marqueurs) -----------------
    VkShaderModule vs = load_shader("scene.vert.spv");
    VkShaderModule fs = load_shader("scene.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_; o.topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      pipe_mesh_ = build_pipeline(vs, fs, o); }
    { PipeOpts o; o.layout = pipe_layout_; o.topo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      o.alpha_blend = true; o.depth_write = false;   // trajectoires fondues (overlay)
      pipe_line_ = build_pipeline(vs, fs, o); }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);

    // --- pipeline materiau planetaire (set 1) --------------------------------
    VkShaderModule pvs = load_shader("planet.vert.spv");
    VkShaderModule pfs = load_shader("planet.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_planet_; o.topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      o.cull = VK_CULL_MODE_NONE;  // robuste (pas d'hypothese sur le winding de la sphere)
      pipe_planet_ = build_pipeline(pvs, pfs, o); }
    vkDestroyShaderModule(device_, pvs, nullptr);
    vkDestroyShaderModule(device_, pfs, nullptr);

    // --- pipeline maillage GLB texture (ISS exterieure) : set 1, opaque -------
    // UV reelles du modele (mesh.vert/frag) ; depth test+write, deux faces (cull
    // NONE, robuste au winding apres transforms de node).
    VkShaderModule mvs = load_shader("mesh.vert.spv");
    VkShaderModule mfs = load_shader("mesh.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_planet_; o.topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      o.cull = VK_CULL_MODE_NONE;
      pipe_mesh_tex_ = build_pipeline(mvs, mfs, o); }
    vkDestroyShaderModule(device_, mvs, nullptr);
    vkDestroyShaderModule(device_, mfs, nullptr);

    // --- pipeline coquille translucide (nuages/atmosphere) : set 1, alpha ------
    VkShaderModule shvs = load_shader("shell.vert.spv");
    VkShaderModule shfs = load_shader("shell.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_planet_; o.topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      o.cull = VK_CULL_MODE_NONE; o.alpha_blend = true; o.depth_write = false;
      pipe_shell_ = build_pipeline(shvs, shfs, o); }
    vkDestroyShaderModule(device_, shvs, nullptr);
    vkDestroyShaderModule(device_, shfs, nullptr);

    // --- pipeline anneau (Saturne) : set 1, alpha, double face ----------------
    VkShaderModule rvs = load_shader("ring.vert.spv");
    VkShaderModule rfs = load_shader("ring.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_planet_; o.topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      o.cull = VK_CULL_MODE_NONE; o.alpha_blend = true; o.depth_write = false;
      pipe_ring_ = build_pipeline(rvs, rfs, o); }
    vkDestroyShaderModule(device_, rvs, nullptr);
    vkDestroyShaderModule(device_, rfs, nullptr);

    // --- pipeline starfield (points, additif, sans depth, dessine en premier) -
    VkShaderModule svs = load_shader("star.vert.spv");
    VkShaderModule sfs = load_shader("star.frag.spv");
    { PipeOpts o; o.layout = pipe_layout_; o.topo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      o.depth_test = false; o.depth_write = false; o.additive = true;
      pipe_star_ = build_pipeline(svs, sfs, o); }
    vkDestroyShaderModule(device_, svs, nullptr);
    vkDestroyShaderModule(device_, sfs, nullptr);
  }

  VkPipeline build_pipeline(VkShaderModule vs, VkShaderModule fs, const PipeOpts& opts) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    // attribut 2 (uv) = ADDITIF : consomme uniquement par le pipeline mesh.* ; les
    // autres shaders (loc 0/1) l'ignorent sans surcout (stride = sizeof(Vertex)).
    VkVertexInputAttributeDescription attr[3]{};
    attr[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    attr[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attr[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)};
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = opts.topo;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = opts.cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = msaa_;   // passe scene : multisample (bords lisses)

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = opts.depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = opts.depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;  // reversed-Z (near=1, far=0)

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (opts.additive) {
      // blend additif : les etoiles s'ajoutent au fond spatial quasi-noir (lueur).
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.colorBlendOp = VK_BLEND_OP_ADD;
      cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (opts.alpha_blend) {
      // blend alpha classique : trajectoires fondues (alpha par sommet).
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      cba.colorBlendOp = VK_BLEND_OP_ADD;
      cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                             VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dsi{};
    dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    // La largeur de trait n'est dynamique que pour les pipelines LIGNE (orbites) :
    // permet l'epaississement de la trajectoire survolee (vkCmdSetLineWidth).
    dsi.dynamicStateCount = (opts.topo == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) ? 3u : 2u;
    dsi.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vin;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dsi;
    ci.layout = opts.layout;
    ci.renderPass = render_pass_;
    ci.subpass = 0;
    VkPipeline p{};
    vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p),
             "vkCreateGraphicsPipelines");
    return p;
  }

  void create_commands() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queue_family_;
    vk_check(vkCreateCommandPool(device_, &pci, nullptr, &cmd_pool_), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmd_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES;
    vk_check(vkAllocateCommandBuffers(device_, &ai, cmd_bufs_), "vkAllocateCommandBuffers");
  }

  void create_sync() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES; ++i) {
      vk_check(vkCreateSemaphore(device_, &si, nullptr, &img_available_[i]), "sem");
      vk_check(vkCreateFence(device_, &fi, nullptr, &in_flight_[i]), "fence");
    }
    render_finished_.resize(image_count_);
    for (std::uint32_t i = 0; i < image_count_; ++i)
      vk_check(vkCreateSemaphore(device_, &si, nullptr, &render_finished_[i]), "sem");
  }

  // =========================== helpers ======================================
  std::uint32_t find_mem_type(std::uint32_t bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
        return i;
    throw std::runtime_error("Aucun type de memoire compatible");
  }

  void create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf,
                          VkDeviceMemory& mem, void** mapped) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(device_, &bci, nullptr, &buf), "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_mem_type(req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vk_check(vkAllocateMemory(device_, &ai, nullptr, &mem), "vkAllocateMemory");
    vkBindBufferMemory(device_, buf, mem, 0);
    if (mapped) vkMapMemory(device_, mem, 0, size, 0, mapped);
  }

  void free_mesh(MeshEntry& e) {
    if (e.vbuf) vkDestroyBuffer(device_, e.vbuf, nullptr);
    if (e.vmem) vkFreeMemory(device_, e.vmem, nullptr);
    if (e.ibuf) vkDestroyBuffer(device_, e.ibuf, nullptr);
    if (e.imem) vkFreeMemory(device_, e.imem, nullptr);
    e = MeshEntry{};
  }

  // =========================== textures / materiaux ==========================
  // Enregistre + soumet un command buffer a usage unique (upload de texture au
  // chargement) et attend sa fin. Simple et sur hors de la boucle de rendu.
  template <class F>
  void run_one_time(F&& record) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmd_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vk_check(vkAllocateCommandBuffers(device_, &ai, &cmd), "vkAllocateCommandBuffers(1time)");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vk_check(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(1time)");
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
  }

  void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                     VkAccessFlags srcA, VkAccessFlags dstA,
                     VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcA;
    b.dstAccessMask = dstA;
    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
  }

  TextureEntry create_texture_internal(const std::uint8_t* px, std::uint32_t w,
                                       std::uint32_t h, VkFormat fmt) {
    TextureEntry e{};
    const VkDeviceSize size = VkDeviceSize(w) * h * 4;

    VkBuffer stg{}; VkDeviceMemory stg_mem{}; void* map = nullptr;
    create_host_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stg, stg_mem, &map);
    std::memcpy(map, px, static_cast<size_t>(size));

    // MIPMAPS : indispensables pour supprimer le scintillement (une carte 8K sans
    // mips aliase severement quand le corps est loin/petit). Genere par blit lineaire
    // si le format le supporte, sinon repli 1 seul niveau.
    std::uint32_t mips = 1;
    {
      VkFormatProperties fp{};
      vkGetPhysicalDeviceFormatProperties(phys_, fmt, &fp);
      const bool can_blit =
          (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
          (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
          (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
      if (can_blit) {
        std::uint32_t m = std::max(w, h);
        while (m > 1) { m >>= 1; ++mips; }
      }
    }

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {w, h, 1};
    ici.mipLevels = mips;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;   // TRANSFER_SRC : source des blits de mip
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_check(vkCreateImage(device_, &ici, nullptr, &e.image), "vkCreateImage(texture)");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, e.image, &req);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vk_check(vkAllocateMemory(device_, &mai, nullptr, &e.mem), "vkAllocateMemory(texture)");
    vkBindImageMemory(device_, e.image, e.mem, 0);

    run_one_time([&](VkCommandBuffer cmd) {
      auto barrier_lvl = [&](std::uint32_t lvl, VkImageLayout oldL, VkImageLayout newL,
                             VkAccessFlags srcA, VkAccessFlags dstA,
                             VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL; b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = e.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, lvl, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
      };
      // tous les niveaux -> TRANSFER_DST
      VkImageMemoryBarrier all{};
      all.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      all.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; all.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      all.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; all.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      all.image = e.image;
      all.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
      all.srcAccessMask = 0; all.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &all);

      VkBufferImageCopy region{};
      region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.imageExtent = {w, h, 1};
      vkCmdCopyBufferToImage(cmd, stg, e.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      std::int32_t mw = static_cast<std::int32_t>(w), mh = static_cast<std::int32_t>(h);
      for (std::uint32_t i = 1; i < mips; ++i) {
        barrier_lvl(i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        const std::int32_t nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
        VkImageBlit blit{};
        blit.srcOffsets[1] = {mw, mh, 1};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        vkCmdBlitImage(cmd, e.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       e.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier_lvl(i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        mw = nw; mh = nh;
      }
      // dernier niveau : TRANSFER_DST -> SHADER_READ
      barrier_lvl(mips - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });

    vkDestroyBuffer(device_, stg, nullptr);
    vkFreeMemory(device_, stg_mem, nullptr);   // libere aussi le mapping
    e.view = make_view(e.image, fmt, VK_IMAGE_ASPECT_COLOR_BIT, mips);
    return e;
  }

  void free_texture(TextureEntry& e) {
    if (e.view) vkDestroyImageView(device_, e.view, nullptr);
    if (e.image) vkDestroyImage(device_, e.image, nullptr);
    if (e.mem) vkFreeMemory(device_, e.mem, nullptr);
    e = TextureEntry{};
  }

  // MaterialParams (API) -> MaterialUbo (std140). Un seul endroit de correspondance.
  void pack_material(const MaterialParams& p, MaterialUbo& u) {
    u.base_color = Vec4{p.base_color, 1.0f};
    u.pbr        = Vec4{p.roughness, p.metallic, p.emissive, p.night_intensity};
    u.extra      = Vec4{p.rim_strength, p.ocean_level, p.detail_scale, 0.0f};
    u.color_low  = Vec4{p.color_low, 0.0f};
    u.color_mid  = Vec4{p.color_mid, 0.0f};
    u.color_high = Vec4{p.color_high, 0.0f};
    u.flags[0] = static_cast<std::int32_t>(p.archetype);
    u.flags[1] = static_cast<std::int32_t>(p.features);
    u.flags[2] = static_cast<std::int32_t>(p.seed);
    u.flags[3] = 0;
  }

  VkImageView view_of(TextureHandle h) {
    auto it = textures_.find(h);
    return (it != textures_.end()) ? it->second.view : VK_NULL_HANDLE;
  }
  VkImageView view_or(TextureHandle h, TextureHandle fallback) {
    VkImageView v = view_of(h);
    return v ? v : view_of(fallback);
  }

  void write_material_set(const MaterialEntry& e, const MaterialDesc& d) {
    VkDescriptorBufferInfo bufi{e.ubo, 0, sizeof(MaterialUbo)};
    const TextureHandle maps[4] = {d.albedo, d.normal, d.rough, d.night};
    const TextureHandle defs[4] = {default_albedo_, default_normal_, default_rough_, default_night_};
    VkDescriptorImageInfo imgs[4]{};
    for (int i = 0; i < 4; ++i) {
      imgs[i].sampler = sampler_;
      imgs[i].imageView = view_or(maps[i], defs[i]);
      imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkWriteDescriptorSet w[5]{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = e.set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bufi;
    for (int i = 0; i < 4; ++i) {
      w[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[i + 1].dstSet = e.set;
      w[i + 1].dstBinding = static_cast<std::uint32_t>(i + 1);
      w[i + 1].descriptorCount = 1;
      w[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i + 1].pImageInfo = &imgs[i];
    }
    vkUpdateDescriptorSets(device_, 5, w, 0, nullptr);
  }

  void free_material(MaterialEntry& e) {
    if (e.set) vkFreeDescriptorSets(device_, mat_pool_, 1, &e.set);
    if (e.ubo) vkDestroyBuffer(device_, e.ubo, nullptr);
    if (e.ubo_mem) vkFreeMemory(device_, e.ubo_mem, nullptr);
    e = MaterialEntry{};
  }

  // Sampler partage + layout set=1 (UBO + 4 samplers) + pool de materiaux.
  void create_material_system() {
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;   // cartes equirectangulaires
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sci.maxLod = VK_LOD_CLAMP_NONE;
    vk_check(vkCreateSampler(device_, &sci, nullptr, &sampler_), "vkCreateSampler");

    VkDescriptorSetLayoutBinding b[5]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (int i = 1; i < 5; ++i)
      b[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
              VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 5;
    lci.pBindings = b;
    vk_check(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &mat_layout_),
             "vkCreateDescriptorSetLayout(material)");

    VkDescriptorPoolSize ps[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_MATERIALS},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_MATERIALS * 4}};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = MAX_MATERIALS;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = ps;
    vk_check(vkCreateDescriptorPool(device_, &pci, nullptr, &mat_pool_),
             "vkCreateDescriptorPool(material)");
  }

  void destroy_material_system() {
    if (mat_pool_) vkDestroyDescriptorPool(device_, mat_pool_, nullptr);
    if (mat_layout_) vkDestroyDescriptorSetLayout(device_, mat_layout_, nullptr);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    mat_pool_ = VK_NULL_HANDLE; mat_layout_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE;
  }

  // Textures par defaut NEUTRES (1x1) liees quand un materiau n'a pas de carte :
  // le set de descripteurs reste complet/valide sans exiger d'asset.
  TextureHandle make_default_texture(const std::uint8_t px[4], bool srgb) {
    TextureDesc d{};
    d.rgba = px; d.width = 1; d.height = 1; d.srgb = srgb;
    return create_texture(d);
  }
  void create_default_textures() {
    static const std::uint8_t white[4]  = {255, 255, 255, 255};  // albedo neutre
    static const std::uint8_t normal[4] = {128, 128, 255, 255};  // normale plate (0,0,1)
    static const std::uint8_t rough[4]  = {200, 0, 0, 255};      // R=rugosite, G=0
    static const std::uint8_t black[4]  = {0, 0, 0, 255};        // pas de nuit
    default_albedo_ = make_default_texture(white, true);
    default_normal_ = make_default_texture(normal, false);
    default_rough_  = make_default_texture(rough, false);
    default_night_  = make_default_texture(black, true);
  }

  // =========================== post-process (composition HDR->LDR) ===========
  VkPipeline build_fullscreen_pipeline(VkShaderModule vs, VkShaderModule fs,
                                       VkPipelineLayout layout, VkRenderPass rp, bool additive) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vin{};  // aucun buffer de sommets
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (additive) {
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.colorBlendOp = VK_BLEND_OP_ADD;
      cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsi{};
    dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vin; ci.pInputAssemblyState = &ia; ci.pViewportState = &vp;
    ci.pRasterizationState = &rs; ci.pMultisampleState = &ms; ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb; ci.pDynamicState = &dsi;
    ci.layout = layout; ci.renderPass = rp; ci.subpass = 0;
    VkPipeline p{};
    vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p),
             "vkCreateGraphicsPipelines(fullscreen)");
    return p;
  }

  void update_post_descriptors() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkDescriptorImageInfo hdr{post_sampler_, hdr_view_[i],
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkImageView bv = bloom_view_[i] ? bloom_view_[i] : view_of(default_night_);
      VkDescriptorImageInfo bloom{post_sampler_, bv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkWriteDescriptorSet w[2]{};
      w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[0].dstSet = post_set_[i]; w[0].dstBinding = 0; w[0].descriptorCount = 1;
      w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &hdr;
      w[1] = w[0]; w[1].dstBinding = 1; w[1].pImageInfo = &bloom;
      vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    }
  }

  void create_post() {
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR; sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    vk_check(vkCreateSampler(device_, &sci, nullptr, &post_sampler_), "vkCreateSampler(post)");

    VkDescriptorSetLayoutBinding b[2]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 2; lci.pBindings = b;
    vk_check(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &post_layout_),
             "vkCreateDescriptorSetLayout(post)");

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES * 2};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = MAX_FRAMES; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
    vk_check(vkCreateDescriptorPool(device_, &pci, nullptr, &post_pool_),
             "vkCreateDescriptorPool(post)");
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = post_pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &post_layout_;
      vk_check(vkAllocateDescriptorSets(device_, &ai, &post_set_[i]), "vkAllocateDescriptorSets(post)");
    }
    update_post_descriptors();

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &post_layout_;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vk_check(vkCreatePipelineLayout(device_, &plci, nullptr, &post_pipe_layout_),
             "vkCreatePipelineLayout(post)");

    VkShaderModule vs = load_shader("fullscreen.vert.spv");
    VkShaderModule fs = load_shader("composite.frag.spv");
    pipe_composite_ = build_fullscreen_pipeline(vs, fs, post_pipe_layout_, rp_post_, false);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
  }

  void destroy_post() {
    if (pipe_composite_) vkDestroyPipeline(device_, pipe_composite_, nullptr);
    if (post_pipe_layout_) vkDestroyPipelineLayout(device_, post_pipe_layout_, nullptr);
    if (post_pool_) vkDestroyDescriptorPool(device_, post_pool_, nullptr);
    if (post_layout_) vkDestroyDescriptorSetLayout(device_, post_layout_, nullptr);
    if (post_sampler_) vkDestroySampler(device_, post_sampler_, nullptr);
    pipe_composite_ = VK_NULL_HANDLE; post_pipe_layout_ = VK_NULL_HANDLE;
    post_pool_ = VK_NULL_HANDLE; post_layout_ = VK_NULL_HANDLE; post_sampler_ = VK_NULL_HANDLE;
  }

  // =========================== bloom (bright + blur separable half-res) ======
  void write_sampler(VkDescriptorSet set, VkImageView view) {
    VkDescriptorImageInfo ii{post_sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
  }

  // Ressources INDEPENDANTES de la taille (passe, layouts, pipelines, sets).
  void create_bloom_pipelines() {
    VkAttachmentDescription col{};
    col.format = HDR_FORMAT; col.samples = VK_SAMPLE_COUNT_1_BIT;
    col.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; col.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    col.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; col.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    col.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; col.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &cref;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = &col; ci.subpassCount = 1; ci.pSubpasses = &sub;
    ci.dependencyCount = static_cast<std::uint32_t>(deps.size()); ci.pDependencies = deps.data();
    vk_check(vkCreateRenderPass(device_, &ci, nullptr, &rp_bloom_), "vkCreateRenderPass(bloom)");

    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1; lci.pBindings = &b;
    vk_check(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &bloom_layout_), "dsl(bloom)");

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPush)};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &bloom_layout_;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vk_check(vkCreatePipelineLayout(device_, &plci, nullptr, &bloom_pipe_layout_), "pl(bloom)");

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES * 3};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = MAX_FRAMES * 3; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
    vk_check(vkCreateDescriptorPool(device_, &pci, nullptr, &bloom_pool_), "pool(bloom)");
    for (int i = 0; i < MAX_FRAMES; ++i) {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = bloom_pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &bloom_layout_;
      vk_check(vkAllocateDescriptorSets(device_, &ai, &bright_set_[i]), "as(bright)");
      vk_check(vkAllocateDescriptorSets(device_, &ai, &blurH_set_[i]), "as(blurH)");
      vk_check(vkAllocateDescriptorSets(device_, &ai, &blurV_set_[i]), "as(blurV)");
    }

    VkShaderModule vs = load_shader("fullscreen.vert.spv");
    VkShaderModule fbright = load_shader("bright.frag.spv");
    VkShaderModule fblur = load_shader("blur.frag.spv");
    pipe_bright_ = build_fullscreen_pipeline(vs, fbright, bloom_pipe_layout_, rp_bloom_, false);
    pipe_blur_   = build_fullscreen_pipeline(vs, fblur, bloom_pipe_layout_, rp_bloom_, false);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fbright, nullptr);
    vkDestroyShaderModule(device_, fblur, nullptr);
  }

  void destroy_bloom_pipelines() {
    if (pipe_bright_) vkDestroyPipeline(device_, pipe_bright_, nullptr);
    if (pipe_blur_) vkDestroyPipeline(device_, pipe_blur_, nullptr);
    if (bloom_pipe_layout_) vkDestroyPipelineLayout(device_, bloom_pipe_layout_, nullptr);
    if (bloom_pool_) vkDestroyDescriptorPool(device_, bloom_pool_, nullptr);
    if (bloom_layout_) vkDestroyDescriptorSetLayout(device_, bloom_layout_, nullptr);
    if (rp_bloom_) vkDestroyRenderPass(device_, rp_bloom_, nullptr);
    pipe_bright_ = pipe_blur_ = VK_NULL_HANDLE; bloom_pipe_layout_ = VK_NULL_HANDLE;
    bloom_pool_ = VK_NULL_HANDLE; bloom_layout_ = VK_NULL_HANDLE; rp_bloom_ = VK_NULL_HANDLE;
  }

  void update_bloom_descriptors() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      write_sampler(bright_set_[i], hdr_view_[i]);       // bright lit la scene HDR
      write_sampler(blurH_set_[i], bloom_iview_[0][i]);  // blurH lit bloom_a
      write_sampler(blurV_set_[i], bloom_iview_[1][i]);  // blurV lit bloom_b
    }
  }

  // Cibles half-res (par-frame, ping-pong a/b). Recreees au resize.
  void create_bloom_targets() {
    bloom_extent_ = {std::max(1u, extent_.width / 2), std::max(1u, extent_.height / 2)};
    for (int p = 0; p < 2; ++p)
      for (int i = 0; i < MAX_FRAMES; ++i) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = HDR_FORMAT;
        ci.extent = {bloom_extent_.width, bloom_extent_.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_check(vkCreateImage(device_, &ci, nullptr, &bloom_img_[p][i]), "vkCreateImage(bloom)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, bloom_img_[p][i], &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &ai, nullptr, &bloom_mem_[p][i]), "vkAllocateMemory(bloom)");
        vkBindImageMemory(device_, bloom_img_[p][i], bloom_mem_[p][i], 0);
        bloom_iview_[p][i] = make_view(bloom_img_[p][i], HDR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = rp_bloom_; fci.attachmentCount = 1; fci.pAttachments = &bloom_iview_[p][i];
        fci.width = bloom_extent_.width; fci.height = bloom_extent_.height; fci.layers = 1;
        vk_check(vkCreateFramebuffer(device_, &fci, nullptr, &fb_bloom_[p][i]), "fb(bloom)");
      }
    for (int i = 0; i < MAX_FRAMES; ++i) bloom_view_[i] = bloom_iview_[0][i];  // composite lit bloom_a
    bloom_ready_ = true;
    update_bloom_descriptors();
    update_post_descriptors();
  }

  void destroy_bloom_targets() {
    for (int p = 0; p < 2; ++p)
      for (int i = 0; i < MAX_FRAMES; ++i) {
        if (fb_bloom_[p][i]) vkDestroyFramebuffer(device_, fb_bloom_[p][i], nullptr);
        if (bloom_iview_[p][i]) vkDestroyImageView(device_, bloom_iview_[p][i], nullptr);
        if (bloom_img_[p][i]) vkDestroyImage(device_, bloom_img_[p][i], nullptr);
        if (bloom_mem_[p][i]) vkFreeMemory(device_, bloom_mem_[p][i], nullptr);
        fb_bloom_[p][i] = VK_NULL_HANDLE; bloom_iview_[p][i] = VK_NULL_HANDLE;
        bloom_img_[p][i] = VK_NULL_HANDLE; bloom_mem_[p][i] = VK_NULL_HANDLE;
      }
    for (int i = 0; i < MAX_FRAMES; ++i) bloom_view_[i] = VK_NULL_HANDLE;
    bloom_ready_ = false;
  }

  void record_bloom(VkCommandBuffer cmd) {
    if (!bloom_ready_) return;
    VkViewport vp{}; vp.width = static_cast<float>(bloom_extent_.width);
    vp.height = static_cast<float>(bloom_extent_.height); vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.extent = bloom_extent_;
    auto do_pass = [&](VkFramebuffer fb, VkPipeline pipe, VkDescriptorSet set, BloomPush push) {
      VkRenderPassBeginInfo rp{};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = rp_bloom_; rp.framebuffer = fb; rp.renderArea.extent = bloom_extent_;
      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloom_pipe_layout_, 0, 1, &set, 0, nullptr);
      vkCmdPushConstants(cmd, bloom_pipe_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      vkCmdEndRenderPass(cmd);
    };
    const float tw = 1.0f / bloom_extent_.width, th = 1.0f / bloom_extent_.height;
    do_pass(fb_bloom_[0][frame_], pipe_bright_, bright_set_[frame_], BloomPush{0, 0, bloom_threshold_, 0});
    do_pass(fb_bloom_[1][frame_], pipe_blur_, blurH_set_[frame_], BloomPush{tw, 0, 0, 0});
    do_pass(fb_bloom_[0][frame_], pipe_blur_, blurV_set_[frame_], BloomPush{0, th, 0, 0});
  }

  // =========================== capture (readback swapchain) ==================
  void ensure_readback_buffer(VkDeviceSize size) {
    if (readback_buf_ && readback_size_ >= size) return;
    if (readback_buf_) {
      vkDestroyBuffer(device_, readback_buf_, nullptr);
      vkFreeMemory(device_, readback_mem_, nullptr);
    }
    create_host_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback_buf_, readback_mem_,
                       &readback_mapped_);
    readback_size_ = size;
  }

  void record_capture_copy(VkCommandBuffer cmd) {
    ensure_readback_buffer(VkDeviceSize(extent_.width) * extent_.height * 4);
    VkImage img = images_[image_index_];
    image_barrier(cmd, img, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  0, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buf_, 1, &region);
    image_barrier(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_ACCESS_TRANSFER_READ_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  }

  // Ecrit un BMP 24 bits (format non compresse, lisible partout). Le readback est
  // en BGRA -> BMP stocke deja en BGR, lignes de bas en haut.
  void write_capture_bmp(const char* path) {
    if (!readback_mapped_) return;
    const auto* px = static_cast<const std::uint8_t*>(readback_mapped_);
    const bool rgba = (swap_format_ == VK_FORMAT_R8G8B8A8_UNORM ||
                       swap_format_ == VK_FORMAT_R8G8B8A8_SRGB);
    const std::uint32_t W = extent_.width, H = extent_.height;
    const std::uint32_t row = W * 3;
    const std::uint32_t pad = (4 - (row % 4)) % 4;
    const std::uint32_t stride = row + pad;
    const std::uint32_t img_size = stride * H;
    const std::uint32_t file_size = 54 + img_size;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    auto put32 = [](std::uint8_t* d, std::uint32_t v) {
      d[0] = v & 0xFF; d[1] = (v >> 8) & 0xFF; d[2] = (v >> 16) & 0xFF; d[3] = (v >> 24) & 0xFF;
    };
    put32(hdr + 2, file_size);
    put32(hdr + 10, 54);           // offset des donnees
    put32(hdr + 14, 40);           // taille de l'info header
    put32(hdr + 18, W);
    put32(hdr + 22, H);            // positif : lignes de bas en haut
    hdr[26] = 1;                   // plans
    hdr[28] = 24;                  // bits par pixel
    put32(hdr + 34, img_size);
    std::fwrite(hdr, 1, 54, f);

    std::vector<std::uint8_t> line(stride, 0);
    for (std::uint32_t y = 0; y < H; ++y) {
      const std::uint32_t src_y = H - 1 - y;   // BMP: de bas en haut
      for (std::uint32_t x = 0; x < W; ++x) {
        const std::uint8_t* p = px + (std::size_t(src_y) * W + x) * 4;
        std::uint8_t* o = line.data() + x * 3;
        if (rgba) { o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; }   // RGBA -> BGR
        else      { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; }   // BGRA -> BGR (direct)
      }
      std::fwrite(line.data(), 1, stride, f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[vk] capture ecrite : %s (%ux%u)\n", path, W, H);
  }

  // =========================== teardown / recreate ===========================
  void recreate_swapchain() {
    resized_ = false;
    vkDeviceWaitIdle(device_);
    destroy_framebuffers();
    destroy_bloom_targets();   // Phase 3 (no-op si non alloue)
    destroy_hdr_targets();
    destroy_msaa_targets();
    destroy_depth();
    destroy_swapchain_views_only();
    // renderFinished depend du nombre d'images : le recreer aussi.
    for (auto s : render_finished_) vkDestroySemaphore(device_, s, nullptr);
    render_finished_.clear();
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }

    create_swapchain();
    create_depth();
    create_hdr_targets();
    create_msaa_targets();
    create_bloom_targets();    // Phase 3
    create_framebuffers();
    update_post_descriptors(); // les vues HDR/bloom ont change -> reecrire les sets
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    render_finished_.resize(image_count_);
    for (std::uint32_t i = 0; i < image_count_; ++i)
      vkCreateSemaphore(device_, &si, nullptr, &render_finished_[i]);
    if (imgui_ready_) ImGui_ImplVulkan_SetMinImageCount(min_image_count_);
  }

  void destroy_swapchain_views_only() {
    for (auto v : image_views_) vkDestroyImageView(device_, v, nullptr);
    image_views_.clear();
  }
  void destroy_swapchain() {
    destroy_swapchain_views_only();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  void destroy_depth() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      if (depth_view_[i]) vkDestroyImageView(device_, depth_view_[i], nullptr);
      if (depth_img_[i]) vkDestroyImage(device_, depth_img_[i], nullptr);
      if (depth_mem_[i]) vkFreeMemory(device_, depth_mem_[i], nullptr);
      depth_view_[i] = VK_NULL_HANDLE; depth_img_[i] = VK_NULL_HANDLE; depth_mem_[i] = VK_NULL_HANDLE;
    }
  }
  void destroy_framebuffers() {
    for (int i = 0; i < MAX_FRAMES; ++i)
      if (fb_scene_[i]) { vkDestroyFramebuffer(device_, fb_scene_[i], nullptr); fb_scene_[i] = VK_NULL_HANDLE; }
    for (auto f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    framebuffers_.clear();
  }
  void destroy_pipelines() {
    if (pipe_mesh_) vkDestroyPipeline(device_, pipe_mesh_, nullptr);
    if (pipe_mesh_tex_) vkDestroyPipeline(device_, pipe_mesh_tex_, nullptr);
    if (pipe_line_) vkDestroyPipeline(device_, pipe_line_, nullptr);
    if (pipe_planet_) vkDestroyPipeline(device_, pipe_planet_, nullptr);
    if (pipe_shell_) vkDestroyPipeline(device_, pipe_shell_, nullptr);
    if (pipe_ring_) vkDestroyPipeline(device_, pipe_ring_, nullptr);
    if (pipe_star_) vkDestroyPipeline(device_, pipe_star_, nullptr);
    if (pipe_layout_) vkDestroyPipelineLayout(device_, pipe_layout_, nullptr);
    if (pipe_layout_planet_) vkDestroyPipelineLayout(device_, pipe_layout_planet_, nullptr);
    pipe_mesh_ = pipe_mesh_tex_ = pipe_line_ = pipe_planet_ = pipe_shell_ = pipe_ring_ = pipe_star_ = VK_NULL_HANDLE;
    pipe_layout_ = pipe_layout_planet_ = VK_NULL_HANDLE;
  }
  void destroy_descriptors_and_ubo() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      if (ubo_buf_[i]) vkDestroyBuffer(device_, ubo_buf_[i], nullptr);
      if (ubo_mem_[i]) vkFreeMemory(device_, ubo_mem_[i], nullptr);
    }
    if (desc_pool_) vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    if (desc_layout_) vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
    desc_pool_ = VK_NULL_HANDLE; desc_layout_ = VK_NULL_HANDLE;
  }
  void destroy_sync() {
    for (int i = 0; i < MAX_FRAMES; ++i) {
      if (img_available_[i]) vkDestroySemaphore(device_, img_available_[i], nullptr);
      if (in_flight_[i]) vkDestroyFence(device_, in_flight_[i], nullptr);
    }
    for (auto s : render_finished_) vkDestroySemaphore(device_, s, nullptr);
    render_finished_.clear();
  }
  void destroy_imgui() {
    if (!imgui_ready_) return;
    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (imgui_pool_) vkDestroyDescriptorPool(device_, imgui_pool_, nullptr);
    imgui_pool_ = VK_NULL_HANDLE;
    imgui_ready_ = false;
  }

  // =========================== etat ==========================================
  DeviceConfig cfg_;
  std::string  device_name_{"?"};

  VkInstance   instance_{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkPhysicalDevice phys_{VK_NULL_HANDLE};
  std::uint32_t queue_family_{0};
  VkDevice     device_{VK_NULL_HANDLE};
  VkQueue      queue_{VK_NULL_HANDLE};

  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  VkFormat       swap_format_{VK_FORMAT_B8G8R8A8_UNORM};
  VkExtent2D     extent_{};
  std::uint32_t  image_count_{0};
  std::uint32_t  min_image_count_{2};
  std::vector<VkImage>     images_;
  std::vector<VkImageView> image_views_;

  // Depth ET cible HDR sont PAR-FRAME (2 frames en vol -> pas d'aliasing).
  VkImage        depth_img_[MAX_FRAMES]{};
  VkDeviceMemory depth_mem_[MAX_FRAMES]{};
  VkImageView    depth_view_[MAX_FRAMES]{};
  VkImage        hdr_img_[MAX_FRAMES]{};
  VkDeviceMemory hdr_mem_[MAX_FRAMES]{};
  VkImageView    hdr_view_[MAX_FRAMES]{};
  // Cible couleur MULTISAMPLE (MSAA) par-frame : la passe scene la resout vers
  // hdr_[i] (anti-aliasing des bords, qualite NASA Eyes). msaa_ = 1 -> desactive.
  VkSampleCountFlagBits msaa_{VK_SAMPLE_COUNT_1_BIT};
  bool           wide_lines_{false};      // wideLines active (largeur de trait > 1)
  float          line_width_max_{1.0f};   // borne haute supportee (survol trajectoire)
  VkImage        msaa_img_[MAX_FRAMES]{};
  VkDeviceMemory msaa_mem_[MAX_FRAMES]{};
  VkImageView    msaa_view_[MAX_FRAMES]{};
  VkFramebuffer  fb_scene_[MAX_FRAMES]{};   // cible HDR + depth (passe scene)

  VkRenderPass   render_pass_{VK_NULL_HANDLE};   // passe SCENE (HDR)
  VkRenderPass   rp_post_{VK_NULL_HANDLE};       // passe PRESENT (swapchain)
  std::vector<VkFramebuffer> framebuffers_;      // par image de swapchain (present)

  // --- composition HDR->LDR (post-process) -----------------------------------
  VkSampler             post_sampler_{VK_NULL_HANDLE};
  VkDescriptorSetLayout post_layout_{VK_NULL_HANDLE};
  VkDescriptorPool      post_pool_{VK_NULL_HANDLE};
  VkDescriptorSet       post_set_[MAX_FRAMES]{};
  VkPipelineLayout      post_pipe_layout_{VK_NULL_HANDLE};
  VkPipeline            pipe_composite_{VK_NULL_HANDLE};
  VkImageView           bloom_view_[MAX_FRAMES]{};   // = bloom_a apres flou (sinon noir)
  bool                  hud_pending_{false};
  float                 exposure_{1.4f};             // exposition (pilotable)
  float                 bloom_strength_{0.06f};      // intensite du bloom (maitrisee)
  float                 bloom_threshold_{1.0f};      // seuil : seul le HDR > 1 deborde

  // --- bloom (bright + blur separable, half-res, par-frame ping-pong) --------
  VkRenderPass          rp_bloom_{VK_NULL_HANDLE};
  VkExtent2D            bloom_extent_{};
  VkImage               bloom_img_[2][MAX_FRAMES]{};
  VkDeviceMemory        bloom_mem_[2][MAX_FRAMES]{};
  VkImageView           bloom_iview_[2][MAX_FRAMES]{};
  VkFramebuffer         fb_bloom_[2][MAX_FRAMES]{};
  VkDescriptorSetLayout bloom_layout_{VK_NULL_HANDLE};
  VkDescriptorPool      bloom_pool_{VK_NULL_HANDLE};
  VkPipelineLayout      bloom_pipe_layout_{VK_NULL_HANDLE};
  VkPipeline            pipe_bright_{VK_NULL_HANDLE};
  VkPipeline            pipe_blur_{VK_NULL_HANDLE};
  VkDescriptorSet       bright_set_[MAX_FRAMES]{};
  VkDescriptorSet       blurH_set_[MAX_FRAMES]{};
  VkDescriptorSet       blurV_set_[MAX_FRAMES]{};
  bool                  bloom_ready_{false};

  VkDescriptorSetLayout desc_layout_{VK_NULL_HANDLE};
  VkDescriptorPool      desc_pool_{VK_NULL_HANDLE};
  VkDescriptorSet       desc_sets_[MAX_FRAMES]{};
  VkBuffer       ubo_buf_[MAX_FRAMES]{};
  VkDeviceMemory ubo_mem_[MAX_FRAMES]{};
  void*          ubo_mapped_[MAX_FRAMES]{};

  VkPipelineLayout pipe_layout_{VK_NULL_HANDLE};
  VkPipelineLayout pipe_layout_planet_{VK_NULL_HANDLE};
  VkPipeline       pipe_mesh_{VK_NULL_HANDLE};
  VkPipeline       pipe_mesh_tex_{VK_NULL_HANDLE};
  VkPipeline       pipe_line_{VK_NULL_HANDLE};
  VkPipeline       pipe_planet_{VK_NULL_HANDLE};
  VkPipeline       pipe_shell_{VK_NULL_HANDLE};
  VkPipeline       pipe_ring_{VK_NULL_HANDLE};
  VkPipeline       pipe_star_{VK_NULL_HANDLE};

  // --- systeme de materiau (set = 1) -----------------------------------------
  VkSampler             sampler_{VK_NULL_HANDLE};
  VkDescriptorSetLayout mat_layout_{VK_NULL_HANDLE};
  VkDescriptorPool      mat_pool_{VK_NULL_HANDLE};
  std::unordered_map<TextureHandle, TextureEntry>   textures_;
  std::unordered_map<MaterialHandle, MaterialEntry> materials_;
  TextureHandle  next_tex_{1};
  MaterialHandle next_mat_{1};
  TextureHandle  default_albedo_{0}, default_normal_{0}, default_rough_{0}, default_night_{0};

  VkCommandPool   cmd_pool_{VK_NULL_HANDLE};
  VkCommandBuffer cmd_bufs_[MAX_FRAMES]{};

  VkSemaphore img_available_[MAX_FRAMES]{};
  std::vector<VkSemaphore> render_finished_;
  VkFence     in_flight_[MAX_FRAMES]{};
  std::vector<VkFence> images_in_flight_;

  std::uint32_t frame_{0};
  std::uint32_t image_index_{0};
  bool          resized_{false};

  // --- capture (readback swapchain -> PPM) -----------------------------------
  bool           capture_supported_{false};
  std::string    capture_path_;
  VkBuffer       readback_buf_{VK_NULL_HANDLE};
  VkDeviceMemory readback_mem_{VK_NULL_HANDLE};
  void*          readback_mapped_{nullptr};
  VkDeviceSize   readback_size_{0};

  VkDescriptorPool imgui_pool_{VK_NULL_HANDLE};
  bool             imgui_ready_{false};

  std::unordered_map<MeshHandle, MeshEntry> meshes_;
  MeshHandle next_handle_{1};
};

} // namespace

std::unique_ptr<IRenderDevice> create_vulkan_device(const DeviceConfig& cfg) {
  return std::make_unique<VulkanDevice>(cfg);
}

} // namespace spr
