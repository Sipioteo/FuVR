package org.vivecraft.client_vr.provider.openvr_lwjgl;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * Bridges Vivecraft/LWJGL's OpenVR binding to the FuVR macOS runtime's
 * {@code libopenvr_api.dylib}.
 *
 * <p>LWJGL's {@code OpenVR.VR_InitInternal} lazily loads {@code openvr_api}
 * via {@code org.lwjgl.system.Library}, which honors the system property
 * {@code org.lwjgl.openvr.libname}. On macOS without SteamVR this discovery
 * fails, so we point LWJGL at the FuVR dylib explicitly before init. Must
 * be called before any LWJGL OpenVR entry point.
 */
public final class FuVRBridge {
    private static final String LWJGL_LIBNAME_PROP = "org.lwjgl.openvr.libname";
    private static volatile boolean loaded = false;

    private FuVRBridge() {}

    public static synchronized void ensureLoaded() {
        if (loaded) return;

        String os = System.getProperty("os.name", "").toLowerCase();
        if (!os.contains("mac")) {
            loaded = true;
            return;
        }

        Object slf4j = tryGetSlf4jLogger();

        List<String> candidates = new ArrayList<>();
        String sysProp = System.getProperty("fuvr.openvr.dylib");
        if (sysProp != null && !sysProp.isEmpty()) candidates.add(sysProp);

        String env = System.getenv("FUVR_OPENVR_DYLIB");
        if (env != null && !env.isEmpty()) candidates.add(env);

        candidates.add("/Library/Application Support/FuVR/lib/libopenvr_api.dylib");
        String home = System.getProperty("user.home");
        if (home != null) {
            candidates.add(home + "/Library/Application Support/FuVR/lib/libopenvr_api.dylib");
        }

        try {
            Class<?> mcClass = Class.forName("net.minecraft.client.Minecraft");
            Object mc = mcClass.getMethod("getInstance").invoke(null);
            if (mc != null) {
                File gameDir = (File) mcClass.getMethod("gameDirectory").invoke(mc);
                if (gameDir != null) {
                    candidates.add(new File(gameDir, "natives/libopenvr_api.dylib").getAbsolutePath());
                    candidates.add(new File(gameDir, "openvr/libopenvr_api.dylib").getAbsolutePath());
                }
            }
        } catch (Throwable ignored) {
            // Minecraft not initialized yet or unavailable; skip sibling fallback.
        }

        String chosen = null;
        for (String c : candidates) {
            File f = new File(c);
            if (f.isFile()) { chosen = f.getAbsolutePath(); break; }
        }

        if (chosen == null) {
            warn(slf4j, "FuVR: no libopenvr_api.dylib candidate found; falling back to LWJGL discovery");
            loaded = true;
            return;
        }

        if (System.getProperty(LWJGL_LIBNAME_PROP) == null) {
            System.setProperty(LWJGL_LIBNAME_PROP, chosen);
        }

        try {
            System.load(chosen);
        } catch (UnsatisfiedLinkError e) {
            warn(slf4j, "FuVR: System.load failed for " + chosen + " (" + e.getMessage() + "); LWJGL will retry");
        }

        // LWJGL's OpenVR.<clinit> independently calls Library.loadSystem("lwjgl_openvr"),
        // expecting a file named exactly "liblwjgl_openvr.dylib" on its library path.
        // Upstream LWJGL never shipped that binding for macOS arm64. Our libopenvr_api.dylib
        // already exports the matching `Java_org_lwjgl_system_JNI_call*` trampolines (76 of
        // them, verified at build time), so we satisfy LWJGL by symlinking liblwjgl_openvr.dylib
        // -> the FuVR dylib in a writable shim directory and prepending that dir to
        // org.lwjgl.librarypath. Loading a symlink to an already-loaded dylib is a no-op at
        // dyld level; the JVM's native-method lookup already sees the trampolines.
        try {
            Path shimDir = Paths.get(System.getProperty("user.home"),
                "Library", "Application Support", "FuVR", "lwjgl-shim");
            Files.createDirectories(shimDir);
            Path shim = shimDir.resolve("liblwjgl_openvr.dylib");
            // Replace any stale link/file. A symlink to the chosen dylib keeps the two
            // images in sync with zero copy cost and updates atomically when FuVR is
            // upgraded (since the dylib lives at a stable path).
            Files.deleteIfExists(shim);
            try {
                Files.createSymbolicLink(shim, Paths.get(chosen));
            } catch (UnsupportedOperationException | java.io.IOException symlinkErr) {
                // Fallback to a copy if the FS rejects symlinks (rare on macOS).
                Files.copy(Paths.get(chosen), shim, StandardCopyOption.REPLACE_EXISTING);
            }

            String shimDirStr = shimDir.toAbsolutePath().toString();
            // System.setProperty alone is NOT sufficient: Minecraft's LWJGL boot
            // (logged as "Backend library: LWJGL version ...") happens long before
            // our hook, and LWJGL's Configuration.LIBRARY_PATH caches its value on
            // first read. The setProperty call is for any code that reads the
            // property fresh; the Configuration.set() reflection override is what
            // actually unblocks `OpenVR.<clinit> -> Library.loadSystem("lwjgl_openvr")`.
            String existing = System.getProperty("org.lwjgl.librarypath", "");
            if (!existing.contains(shimDirStr)) {
                String combined = existing.isEmpty() ? shimDirStr : (shimDirStr + File.pathSeparator + existing);
                System.setProperty("org.lwjgl.librarypath", combined);
            }
            try {
                Class<?> cfg = Class.forName("org.lwjgl.system.Configuration");
                Object libPathCfg = cfg.getField("LIBRARY_PATH").get(null);
                Object current = cfg.getMethod("get").invoke(libPathCfg);
                String currentStr = current == null ? "" : current.toString();
                if (!currentStr.contains(shimDirStr)) {
                    String merged = currentStr.isEmpty()
                        ? shimDirStr
                        : (shimDirStr + File.pathSeparator + currentStr);
                    cfg.getMethod("set", Object.class).invoke(libPathCfg, merged);
                    info(slf4j, "FuVR: Configuration.LIBRARY_PATH set to " + merged);
                }
            } catch (Throwable t) {
                warn(slf4j, "FuVR: could not override LWJGL Configuration.LIBRARY_PATH ("
                    + t.getMessage() + "); relying on system property fallback");
            }
            info(slf4j, "FuVR: shimmed liblwjgl_openvr.dylib at " + shim);
        } catch (Throwable t) {
            warn(slf4j, "FuVR: failed to install liblwjgl_openvr shim (" + t.getMessage()
                + "); OpenVR.<clinit> will likely fail on macOS arm64");
        }

        info(slf4j, "FuVR: using OpenVR dylib at " + chosen);
        loaded = true;
    }

    private static Object tryGetSlf4jLogger() {
        try {
            Class<?> cls = Class.forName("org.vivecraft.client.VivecraftVRMod");
            return cls.getField("LOGGER").get(null);
        } catch (Throwable t) {
            return null;
        }
    }

    private static void info(Object slf4j, String msg) {
        if (slf4j != null) {
            try { slf4j.getClass().getMethod("info", String.class).invoke(slf4j, msg); return; }
            catch (Throwable ignored) {}
        }
        Logger.getLogger("FuVRBridge").info(msg);
    }

    private static void warn(Object slf4j, String msg) {
        if (slf4j != null) {
            try { slf4j.getClass().getMethod("warn", String.class).invoke(slf4j, msg); return; }
            catch (Throwable ignored) {}
        }
        Logger.getLogger("FuVRBridge").log(Level.WARNING, msg);
    }
}
