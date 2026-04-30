package org.vivecraft.mod_compat_vr.curios.mixin;

import com.llamalad7.mixinextras.sugar.Local;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.vivecraft.client_vr.render.helpers.VREffectsHelper;

@Pseudo
@Mixin(targets = "top.theillusivec4.curios.client.render.CuriosLayer")
public class CuriosLayerVRMixin {
    @Inject(method = "lambda$render$1(Lnet/minecraft/world/entity/LivingEntity;Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/MultiBufferSource;IFFFFFFLjava/lang/String;Ltop/theillusivec4/curios/api/type/inventory/ICurioStacksHandler;)V", at = @At("HEAD"), cancellable = true)
    private void vivecraft$noHeadInFirstPerson(
        CallbackInfo ci, @Local(argsOnly = true) LivingEntity entity, @Local(argsOnly = true) String id)
    {
        if ("head".equals(id) && VREffectsHelper.isRenderingFirstPersonPlayer(entity)) {
            ci.cancel();
        }
    }
}
