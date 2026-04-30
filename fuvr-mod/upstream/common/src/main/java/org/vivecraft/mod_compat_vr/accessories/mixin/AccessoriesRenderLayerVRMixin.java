package org.vivecraft.mod_compat_vr.accessories.mixin;

import com.llamalad7.mixinextras.injector.ModifyExpressionValue;
import com.llamalad7.mixinextras.sugar.Local;
import io.wispforest.accessories.api.AccessoriesContainer;
import net.minecraft.client.model.EntityModel;
import net.minecraft.client.renderer.entity.RenderLayerParent;
import net.minecraft.client.renderer.entity.layers.RenderLayer;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.vivecraft.client_vr.render.helpers.VREffectsHelper;

@Pseudo
@Mixin(targets = "io.wispforest.accessories.client.AccessoriesRenderLayer")
public abstract class AccessoriesRenderLayerVRMixin<T extends LivingEntity, M extends EntityModel<T>> extends RenderLayer<T, M> {

    public AccessoriesRenderLayerVRMixin(RenderLayerParent<T, M> renderer) {
        super(renderer);
    }

    @ModifyExpressionValue(method = "render(Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/MultiBufferSource;ILnet/minecraft/world/entity/LivingEntity;FFFFFF)V", at = @At(value = "INVOKE", target = "Lio/wispforest/accessories/api/client/AccessoryRenderer;shouldRender(Z)Z", remap = false), remap = true)
    private boolean vivecraft$noHeadInFirstPerson(
        boolean original, @Local(argsOnly = true) LivingEntity entity,
        @Local AccessoriesContainer container)
    {
        return original && (!VREffectsHelper.isRenderingFirstPersonPlayer(entity) ||
            !(container.getSlotName().contains("head") || container.getSlotName().contains("face") ||
                container.getSlotName().contains("hat")
            )
        );
    }
}
