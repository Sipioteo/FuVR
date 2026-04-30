package org.vivecraft.forge.mixin.network;

import net.minecraftforge.network.PlayMessages;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.vivecraft.client_vr.gameplay.screenhandlers.GuiHandler;

@Mixin(PlayMessages.OpenContainer.class)
public class ForgeOpenContainerVRMixin {
    @Inject(method = "handle", at = @At("HEAD"), remap = false)
    private static void vivecraft$markScreenActiveForge(CallbackInfo ci) {
        GuiHandler.GUI_APPEAR_OVER_BLOCK_ACTIVE = true;
    }
}
