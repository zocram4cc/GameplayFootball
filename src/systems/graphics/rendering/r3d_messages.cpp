// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "r3d_messages.hpp"

#include "autoexposure.hpp"

#include "main.hpp"

#include "../resources/texture.hpp"
#include "../scenegrade.hpp"

namespace blunted {

bool R3DM_SortVertexBufferQueueEntries(const VertexBufferQueueEntry& vb1,
                                       const VertexBufferQueueEntry& vb2) {
  return vb1.vertexBuffer->GetResource()->GetID() < vb2.vertexBuffer->GetResource()->GetID();
}

bool Renderer3DMessage_RenderView::Execute(void* caller) {
  Renderer3D* renderer = static_cast<Renderer3D*>(caller);

  renderer->ClearBuffer(Vector3(0, 0, 0), false, true);

  View& view = renderer->GetView(viewID);

  Matrix4 projectionMatrix = renderer->CreatePerspectiveMatrix(
      view.width / (view.height * 1.0f), buffer.cameraNearCap, buffer.cameraFarCap);
  Matrix4 viewMatrix = buffer.cameraMatrix;

  // not sorting actually seems to be fastest atm. (the sorting being slower than the performance
  // win by reducing state changes)
  // std::sort(buffer.visibleGeometry.begin(), buffer.visibleGeometry.end(),
  // R3DM_SortVertexBufferQueueEntries); printf("%i entries\n", buffer.visibleGeometry.size());
  // buffer.visibleGeometry.sort(SortVertexBufferQueueEntries);

  int width;
  int height;
  int bpp;
  renderer->GetContextSize(width, height, bpp);
  // opengl window starts lower left, so invert y
  // printf("%i %i %i %i\n", view.x, height - view.y, view.width, view.height);
  renderer->SetViewport(view.x, height - (view.y + view.height), view.width, view.height);

  renderer->SetFOV(buffer.cameraFOV);

  float depthParamNear = buffer.cameraFarCap / (buffer.cameraFarCap - buffer.cameraNearCap);
  float depthParamFar =
      (buffer.cameraFarCap * buffer.cameraNearCap) / (buffer.cameraNearCap - buffer.cameraFarCap);

  std::vector<e_TargetAttachment> targets;

  // render skybox

  if (buffer.skyboxes.size() > 0) {  // todo: use shader?
    Matrix4 skyboxMatrix = viewMatrix;
    skyboxMatrix.SetTranslation(Vector3(0, 0, 0));
    // XX renderer->SetMatrixMode(e_MatrixMode_ModelView);
    // XX renderer->LoadMatrix(skyboxMatrix);

    renderer->UseShader("");

    // renderer->SetMatrix("viewMatrix", buffer.cameraMatrix);

    targets.push_back(e_TargetAttachment_Back);
    renderer->SetRenderTargets(targets);
    targets.clear();

    // renderer->ClearBuffer(Vector3(0, 0, 0), true, true);
    renderer->SetCullingMode(e_CullingMode_Back);
    renderer->SetBlendingMode(e_BlendingMode_Off);
    renderer->SetDepthFunction(e_DepthFunction_Less);
    renderer->SetDepthTesting(false);
    renderer->SetDepthMask(false);
    renderer->RenderVertexBuffer(buffer.skyboxes, e_RenderMode_Diffuse);
    renderer->SetDepthMask(true);
    renderer->SetDepthTesting(true);
  }

  // render vertexbuffers

  // pre z phase

  // disabled: geometry phase is already doing this, right?
  // enabled again: the geometry phase writes to the gbuffer, this writes to the accumbuffer that
  // the lighting uses disabled again: seems to work without z-phase, even with ambient phase on z
  // equal. i guess the depth buffer is kept indepedently of the bound frame buffer. disabled again:
  // alpha pass (lame transparency) won't work with this

  bool zphase = false;
  if (zphase) {
    // XXrenderer->SetMatrixMode(e_MatrixMode_ModelView);

    renderer->SetMatrix("projectionMatrix", projectionMatrix);
    renderer->SetMatrix("viewMatrix", viewMatrix);

    renderer->UseShader("zphase");

    renderer->BindFrameBuffer(view.gBufferID);

    targets.push_back(e_TargetAttachment_None);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->SetCullingMode(e_CullingMode_Back);
    renderer->SetBlendingMode(e_BlendingMode_Off);
    renderer->SetDepthFunction(e_DepthFunction_Less);
    renderer->SetDepthTesting(true);
    renderer->SetDepthMask(true);
    renderer->ClearBuffer(Vector3(0, 0, 0), true, false);
    renderer->RenderVertexBuffer(buffer.visibleGeometry, e_RenderMode_GeometryOnly);
  }

  // geometry phase

  renderer->UseShader("simple");

  renderer->BindFrameBuffer(view.gBufferID);

  // framebuffer starts at lower left 0,0
  renderer->SetViewport(0, 0, view.width, view.height);

  renderer->SetMatrix("projectionMatrix", projectionMatrix);
  renderer->SetMatrix("viewMatrix", viewMatrix);

  // work-around ati bug: clearing z with color buffers attached could be slow
  // http://www.infinity-universe.com/Infinity/index.php?option=com_content&task=view&id=105&Itemid=27
  // targets.push_back(e_TargetAttachment_Depth);

  targets.push_back(e_TargetAttachment_Color0);
  targets.push_back(e_TargetAttachment_Color1);
  targets.push_back(e_TargetAttachment_Color2);
  renderer->SetRenderTargets(targets);
  targets.clear();

  renderer->SetCullingMode(e_CullingMode_Back);
  renderer->SetBlendingMode(e_BlendingMode_Off);
  renderer->SetDepthTesting(true);

  if (zphase) {
    renderer->SetDepthFunction(e_DepthFunction_Equal);     // changed from less
    renderer->SetDepthMask(false);                         // changed
    renderer->ClearBuffer(Vector3(0, 0, 0), false, true);  // changed
  } else {
    renderer->SetDepthFunction(e_DepthFunction_Less);
    renderer->SetDepthMask(true);
    renderer->ClearBuffer(Vector3(0, 0, 0), true, true);
  }

  renderer->RenderVertexBuffer(buffer.visibleGeometry, e_RenderMode_Full);

  // lighting phase

  // output goes to accumulation buffer
  renderer->BindFrameBuffer(view.accumBufferID);
  targets.push_back(e_TargetAttachment_Color0);
  targets.push_back(e_TargetAttachment_Color1);
  renderer->SetRenderTargets(targets);
  targets.clear();

  // blend the remaining
  renderer->SetTextureUnit(1);
  renderer->BindTexture(view.gBuffer_NormalTexID);
  renderer->SetTextureUnit(2);
  renderer->BindTexture(view.gBuffer_DepthTexID);
  renderer->SetTextureUnit(3);
  renderer->BindTexture(view.gBuffer_AuxTexID);
  renderer->SetTextureUnit(0);
  renderer->BindTexture(view.gBuffer_AlbedoTexID);

  // renderer->ClearBuffer(Vector3(0, 0, 0), false, true);

  // ambient
  renderer->UseShader("ambient");

  renderer->SetUniformFloat("ambient", "contextWidth", (float)view.width);
  renderer->SetUniformFloat("ambient", "contextHeight", (float)view.height);
  renderer->SetUniformFloat("ambient", "contextX", (float)0);
  renderer->SetUniformFloat("ambient", "contextY", (float)0);
  renderer->SetUniformFloat2("ambient", "cameraClip", depthParamNear, depthParamFar);
  Matrix4 inverseProjectionViewMatrix = (projectionMatrix * viewMatrix).GetInverse();
  renderer->SetUniformMatrix4("ambient", "inverseProjectionViewMatrix",
                              inverseProjectionViewMatrix);
  renderer->SetUniformMatrix4("ambient", "projectionMatrix", projectionMatrix);
  renderer->SetUniformMatrix4("ambient", "viewMatrix", viewMatrix);

  renderer->SetDepthTesting(false);
  renderer->SetDepthMask(false);

  renderer->RenderOverlay2D();

  // lights
  // renderer->SetMatrixMode(e_MatrixMode_ModelView);

  renderer->UseShader("lighting");

  renderer->SetBlendingMode(e_BlendingMode_On);
  renderer->SetBlendingFunction(e_BlendingFunction_One, e_BlendingFunction_One);

  renderer->SetUniformFloat("lighting", "contextWidth", (float)view.width);
  renderer->SetUniformFloat("lighting", "contextHeight", (float)view.height);
  renderer->SetUniformFloat("lighting", "contextX", (float)0);
  renderer->SetUniformFloat("lighting", "contextY", (float)0);
  renderer->SetUniformMatrix4("lighting", "inverseProjectionViewMatrix",
                              inverseProjectionViewMatrix);
  renderer->SetUniformMatrix4("lighting", "projectionMatrix", projectionMatrix);
  renderer->SetUniformMatrix4("lighting", "viewMatrix", viewMatrix);

  // renderer->SetUniformFloat2("lighting", "cameraClip", depthParamNear, depthParamFar);

  renderer->SetDepthTesting(false);
  renderer->SetDepthMask(false);

  renderer->RenderLights(buffer.visibleLights, projectionMatrix, viewMatrix);

  renderer->SetBlendingMode(e_BlendingMode_Off);
  renderer->SetDepthMask(true);
  renderer->SetCullingMode(e_CullingMode_Off);

  renderer->SetTextureUnit(1);
  renderer->BindTexture(0);
  renderer->SetTextureUnit(2);
  renderer->BindTexture(0);
  renderer->SetTextureUnit(3);
  renderer->BindTexture(0);
  renderer->SetTextureUnit(0);
  renderer->BindTexture(0);

  // render accumulation buffer with some nice postprocessing effects

  renderer->BindFrameBuffer(0);

  targets.push_back(e_TargetAttachment_Back);
  renderer->SetRenderTargets(targets);
  targets.clear();

  renderer->UseShader("postprocess");

  // renderer->SetUniformFloat("postprocess", "brightness",
  // (float)renderer->HDRGetOverallBrightness());

  renderer->SetUniformFloat("postprocess", "contextWidth", (float)view.width);
  renderer->SetUniformFloat("postprocess", "contextHeight", (float)view.height);
  renderer->SetUniformFloat("postprocess", "contextX", (float)view.x);
  renderer->SetUniformFloat("postprocess", "contextY", (float)(height - (view.y + view.height)));
  renderer->SetUniformFloat2("postprocess", "cameraClip", depthParamNear, depthParamFar);
  renderer->SetUniformFloat("postprocess", "fogScale",
                            0.8f - NormalizedClamp(buffer.cameraFOV, 20, 100) * 0.6f);
  // The sky the stadium asked for, or the shader's own if it asked for nothing.
  // Match reads these off the stadium when it loads it (stadiumsky.hpp).
  renderer->SetUniformFloat3(
      "postprocess", "skyZenithColor", GetConfiguration()->GetReal("sky_zenith_r", 0.32f),
      GetConfiguration()->GetReal("sky_zenith_g", 0.52f),
      GetConfiguration()->GetReal("sky_zenith_b", 0.78f));
  renderer->SetUniformFloat3(
      "postprocess", "skyHorizonColor", GetConfiguration()->GetReal("sky_horizon_r", 0.78f),
      GetConfiguration()->GetReal("sky_horizon_g", 0.85f),
      GetConfiguration()->GetReal("sky_horizon_b", 0.93f));
  renderer->SetUniformFloat("postprocess", "sceneBrightness",
                            GetConfiguration()->GetReal("graphics_brightness", 1.0f));
  // PES's grade, chosen the way PES chooses it: by time of day and weather
  // (scenegrade.hpp). On, now that the strip carries tables that can be a display
  // transfer at all.
  //
  // It was flattening the picture, and not because the decode was wrong - the ftex
  // read matches PES bit for bit. Of the sixteen tables PES ships, eleven stop
  // climbing around 0.69: lut_s_day_game, which lut_strip.py used to take on the
  // strength of its name, maps grey 0.5 to 0.616 and grey 1.0 to 0.689, so half the
  // input range lands inside a 0.07 band. Applied as a display transfer that costs
  // the frame its whole top end - on st011, p98 0.659 against 0.918 ungraded and
  // 0.918 in the reference broadcast, with the spread down to 0.087 from 0.132. A
  // contrast error, not a level one, which is why keying the exposure never touched
  // it. The importer now picks per band by what a table does to grey, and only
  // lut_h_day_demo and the night tables reach white (cloudy and evening borrow the
  // day one - PES ships nothing usable for them).
  //
  // Measured over all nine converted grounds, as distance from the reference's whole
  // ladder (median, p90, p98, spread): 3.44 graded against 3.49 ungraded, closer on
  // five of the nine. So it is a wash on the numbers - it helps most where a ground
  // is dark (st002 0.92 -> 0.82, st043 0.74 -> 0.59, st041 0.28 -> 0.18) and costs a
  // little where one is already bright (st019 0.15 -> 0.27, st031 0.21 -> 0.37) - and
  // PES's own colour is the tie-breaker: side by side the old table is milky, this
  // one holds Namek's rocks pink and st011's grass green.
  renderer->SetUniformFloat("postprocess", "lutStrength",
                            GetConfiguration()->GetReal("graphics_lut_strength", 1.0f));
  renderer->SetUniformFloat(
      "postprocess", "lutBand",
      (float)SceneGrade::BandForConditions(GetConfiguration()->GetReal("match_time_of_day", 0.0f),
                                           GetConfiguration()->GetReal("match_weather", 0.0f)));
  // Exposure. PES scales every frame so its brightness lands on a key value -
  // gameKeyValue in the atmosphere - and without that each ground is lit to
  // whatever its own sun and textures happen to give: Planet Namek came out at
  // half the broadcast's midtone while st041 was already on it. The key here is
  // that brightness as displayed (the reference measures 0.45), and the gains
  // bound how far a ground may be moved so a night match stays a night match.
  // Exposure, the way PES sets it: the frame is scaled so its average brightness sits
  // at a key value - what its atmosphere calls gameKeyValue, with
  // gameMinExposure/gameMaxExposure bounding the gain. Without it every ground is lit
  // to whatever its own sun and textures happen to give, and against the broadcast
  // reference Planet Namek came out at half the midtone while st031 and st041 were
  // already there.
  //
  // It is worked out here rather than in the shader, and that is the whole point. The
  // first version measured sixteen taps inside postprocess.frag and applied the
  // result the same frame; a fragment shader remembers nothing, so the gain was
  // recomputed from scratch every frame and jumped with every pan. Off a recorded
  // match, the picture's mean brightness moved 0.02 frame to frame through the opening
  // cutscene, with single-frame jumps of -0.073 and +0.038 - a flicker on every cut.
  //
  // Now the renderer measures the frame it just presented (a centre window of the back
  // buffer, every third frame) and the gain walks toward what that asks for over a
  // half-life, the way an eye adapts. graphics_exposure_half_life is that time in
  // seconds; 0 turns the smoothing off and 0 for the key turns the whole thing off.
  {
    const float key = GetConfiguration()->GetReal("graphics_exposure_key", 0.45f);
    const float minGain = GetConfiguration()->GetReal("graphics_exposure_min_gain", 0.55f);
    const float maxGain = GetConfiguration()->GetReal("graphics_exposure_max_gain", 1.6f);
    const float halfLife = GetConfiguration()->GetReal("graphics_exposure_half_life", 1.2f);
    static float gain = 1.0f;
    static unsigned long previousTime_ms = 0;
    const unsigned long now_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const float dt = previousTime_ms == 0
                         ? 0.0f
                         : (now_ms - previousTime_ms) / 1000.0f;
    previousTime_ms = now_ms;
    if (key <= 0.0f) {
      gain = 1.0f;
    } else {
      const float target =
          AutoExposure::TargetGain(AutoExposure::GetMeasuredBrightness(), key, minGain, maxGain);
      gain = AutoExposure::Adapt(gain, target, dt, halfLife);
    }
    renderer->SetUniformFloat("postprocess", "exposureGain", gain);
  }
  // How much of the horizon's colour the distance is washed with. A converted
  // ground sets this from its own atmosphere (influenceOfFog, via lighting.txt),
  // and every PES atmosphere we can actually read says none of it: Planet Namek,
  // benuldys and WWELIAS all carry fog 0. The default used to be full, which put a
  // quarter of a pale grey sky over everything on the six grounds whose lighting
  // comes out of a cpk as PES's own binaries with no readable XML - the haze on
  // their pitches. Off unless a ground asks for it.
  renderer->SetUniformFloat("postprocess", "fogStrength",
                            GetConfiguration()->GetReal("graphics_fog_strength", 0.0f));
  renderer->SetUniformFloat3(
      "postprocess", "skyFogColor", GetConfiguration()->GetReal("sky_fog_r", 0.85f),
      GetConfiguration()->GetReal("sky_fog_g", 0.85f),
      GetConfiguration()->GetReal("sky_fog_b", 0.90f));
  // the sky gradient needs per-pixel view directions
  renderer->SetUniformMatrix4("postprocess", "inverseProjectionViewMatrix",
                              inverseProjectionViewMatrix);

  renderer->SetViewport(view.x, height - (view.y + view.height), view.width, view.height);

  renderer->SetTextureUnit(2);
  renderer->BindTexture(view.gBuffer_DepthTexID);
  renderer->SetTextureUnit(1);
  renderer->BindTexture(view.accumBuffer_ModifierTexID);
  renderer->SetTextureUnit(0);
  renderer->BindTexture(view.accumBuffer_AccumTexID);

  renderer->SetDepthTesting(false);
  renderer->SetDepthMask(false);

  renderer->SetFramebufferGammaCorrection(true);
  renderer->RenderOverlay2D();
  renderer->SetFramebufferGammaCorrection(false);

  renderer->SetTextureUnit(2);
  renderer->BindTexture(0);
  renderer->SetTextureUnit(1);
  renderer->BindTexture(0);
  renderer->SetTextureUnit(0);
  renderer->BindTexture(0);

  // too slow
  // renderer->HDRCaptureOverallBrightness();

  // back to the context viewport
  renderer->UseShader("");

  targets.push_back(e_TargetAttachment_Back);
  renderer->SetRenderTargets(targets);
  targets.clear();

  renderer->SetViewport(0, 0, width, height);

  buffer.visibleGeometry.clear();
  buffer.visibleLights.clear();
  buffer.skyboxes.clear();

  return true;
}

bool Renderer3DMessage_RenderShadowMap::Execute(void* caller) {
  Renderer3D* renderer = static_cast<Renderer3D*>(caller);

  // SORTING update: because we pass a const reference, we need to sort vertexbuffers in
  // graphics_light (or wherever we invoke this)

  // disabled because it's slower to sort (vector) than to switch vertexbuffers
  // std::sort(map.visibleGeometry.begin(), map.visibleGeometry.end(),
  // R3DM_SortVertexBufferQueueEntries); std::sort(map.visibleGeometry.begin(),
  // map.visibleGeometry.end(), SortVertexBufferQueueEntries);

  // sorting a list however might be ok. needs to be tested in a situation with lots of different
  // id's
  // map.visibleGeometry.sort(R3DM_SortVertexBufferQueueEntries);

  renderer->UseShader("zphase");

  // renderer->SetOrtho(-30, 30, -30, 30, 90, 110);
  // renderer->SetFOV(40);
  // renderer->SetPerspective(1.0, 90.0, 110.0);

  renderer->BindFrameBuffer(map.frameBufferID);

  int shadowW, shadowH;
  map.texture->GetResource()->GetSize(shadowW, shadowH);
  renderer->SetViewport(0, 0, shadowW, shadowH);

  renderer->ClearBuffer(Vector3(0, 0, 0), true, false);

  renderer->SetMatrix("projectionMatrix", map.lightProjectionMatrix);
  renderer->SetMatrix("viewMatrix", map.lightViewMatrix);

  std::vector<e_TargetAttachment> targets;
  targets.push_back(e_TargetAttachment_None);
  renderer->SetRenderTargets(targets);
  targets.clear();

  renderer->SetCullingMode(e_CullingMode_Front);
  renderer->SetBlendingMode(e_BlendingMode_Off);
  renderer->SetDepthFunction(e_DepthFunction_Less);
  renderer->SetDepthTesting(true);
  renderer->SetDepthMask(true);

  renderer->RenderVertexBuffer(map.visibleGeometry, e_RenderMode_GeometryOnly);

  renderer->BindFrameBuffer(0);
  renderer->UseShader("");

  targets.push_back(e_TargetAttachment_Back);
  renderer->SetRenderTargets(targets);
  targets.clear();

  // restore context viewport
  int width, height, bpp;
  renderer->GetContextSize(width, height, bpp);
  renderer->SetViewport(0, 0, width, height);
  // renderer->SetPerspective(width / (height * 1.0));

  renderer->SetCullingMode(e_CullingMode_Back);

  return true;
}

bool Renderer3DMessage_CreateFrameBuffer::Execute(void* caller) {
  Renderer3D* renderer = static_cast<Renderer3D*>(caller);

  frameBufferID = renderer->CreateFrameBuffer();
  renderer->BindFrameBuffer(frameBufferID);

  // texture buffers
  if (target1 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target1, texID1);
  if (target2 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target2, texID2);
  if (target3 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target3, texID3);
  if (target4 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target4, texID4);
  if (target5 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target5, texID5);

  // all draw buffers must specify attachment points that have images attached. so to be sure,
  // select none
  std::vector<e_TargetAttachment> targets;
  targets.push_back(e_TargetAttachment_None);
  renderer->SetRenderTargets(targets);
  targets.clear();

  if (!renderer->CheckFrameBufferStatus())
    Log(e_FatalError, "Renderer3DMessage_CreateFrameBuffer", "Execute",
        "Could not create framebuffer");

  renderer->BindFrameBuffer(0);

  targets.push_back(e_TargetAttachment_Back);
  renderer->SetRenderTargets(targets);
  targets.clear();

  return true;
}

bool Renderer3DMessage_DeleteFrameBuffer::Execute(void* caller) {
  Renderer3D* renderer = static_cast<Renderer3D*>(caller);

  renderer->BindFrameBuffer(frameBufferID);
  if (target1 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target1, 0);
  if (target2 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target2, 0);
  if (target3 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target3, 0);
  if (target4 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target4, 0);
  if (target5 != e_TargetAttachment_None)
    renderer->SetFrameBufferTexture2D(target5, 0);
  renderer->DeleteFrameBuffer(frameBufferID);

  return true;
}

}  // namespace blunted
