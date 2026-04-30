package org.vivecraft.mixin.client_vr.renderer;

import com.llamalad7.mixinextras.injector.ModifyExpressionValue;
import net.minecraft.client.renderer.LevelRenderer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.vivecraft.client_vr.VRState;
import org.vivecraft.mixin.client.blaze3d.RenderSystemAccessor;

import java.util.concurrent.atomic.AtomicBoolean;

@Mixin(LevelRenderer.class)
public class NoSodiumLevelRendererVRMixin {

    @Shadow
    private boolean needsFullRenderChunkUpdate;

    @Shadow
    @Final
    private AtomicBoolean needsFrustumUpdate;

    @Inject(method = "setupRender", at = @At(value = "FIELD", target = "Lnet/minecraft/client/renderer/LevelRenderer;needsFullRenderChunkUpdate:Z", ordinal = 1, shift = At.Shift.AFTER))
    private void vivecraft$alwaysUpdateCull(CallbackInfo ci) {
        if (VRState.VR_RUNNING) {
            // if VR is on, always update the frustum, to fix flickering chunks between eyes
            this.needsFullRenderChunkUpdate = true;
            this.needsFrustumUpdate.set(true);
        }
    }

    @ModifyExpressionValue(method = "renderChunkLayer", at = @At(value = "CONSTANT", args = "intValue=12"))
    private int vivecraft$moreTextures(int constant) {
        return Math.max(constant, RenderSystemAccessor.getShaderTextures().length);
    }
}
