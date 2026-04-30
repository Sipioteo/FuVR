package org.vivecraft.mod_compat_vr.trinkets.mixin;

import com.llamalad7.mixinextras.sugar.Local;
import dev.emi.trinkets.api.SlotReference;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.vivecraft.client_vr.render.helpers.VREffectsHelper;

@Pseudo
@Mixin(targets = "dev.emi.trinkets.TrinketFeatureRenderer")
public class TrinketFeatureRendererVRMixin {

    @Inject(method = "lambda$render$1(Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/MultiBufferSource;ILnet/minecraft/world/entity/LivingEntity;FFFFFFLdev/emi/trinkets/api/SlotReference;Lnet/minecraft/world/item/ItemStack;)V", at = @At("HEAD"), cancellable = true)
    private void vivecraft$noHeadInFirstPerson(
        CallbackInfo ci, @Local(argsOnly = true) LivingEntity entity, @Local(argsOnly = true) SlotReference reference)
    {
        if ("head".equals(reference.inventory().getSlotType().getGroup()) &&
            VREffectsHelper.isRenderingFirstPersonPlayer(entity))
        {
            ci.cancel();
        }
    }
}
