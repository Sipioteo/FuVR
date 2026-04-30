// SPDX-License-Identifier: Apache-2.0
import Foundation

/// Installs the bundled mock `libopenvr_api.dylib` into a Vivecraft (or any
/// Minecraft launcher's) `natives/` directory so the game's LWJGL loader
/// picks up FuVR's translation layer instead of Valve's SteamVR runtime.
///
/// Why this exists: the user must NEVER copy files in a terminal. The app
/// scans the well-known launcher install paths, surfaces a plain-language
/// list, and performs the install with a single click. Custom paths are
/// supported via an NSOpenPanel.
public struct OpenVrBridgeInstaller: Sendable {

    /// One discovered installation candidate.
    public struct Candidate: Equatable, Sendable, Identifiable {
        public var id: String { nativesDirectory.path }
        /// Human-readable launcher / instance name (e.g. "Vanilla Launcher",
        /// "PrismLauncher · 1.20.4-vivecraft").
        public let label: String
        /// Directory where `libopenvr_api.dylib` will land — typically the
        /// `natives/` folder LWJGL extracts platform binaries into.
        public let nativesDirectory: URL
        /// Whether a previous version of the dylib is already present.
        public let alreadyInstalled: Bool
    }

    public enum InstallError: Error, CustomStringConvertible {
        case shimNotBundled
        case nativesDirectoryMissing(URL)
        case copyFailed(URL, Error)

        public var description: String {
            switch self {
            case .shimNotBundled:
                return "The FuVR app is missing its libopenvr_api.dylib bundle. Rebuild the openvr_api target."
            case .nativesDirectoryMissing(let u):
                return "The natives directory \(u.path) does not exist or is not writable."
            case .copyFailed(let u, let e):
                return "Couldn't copy the bridge into \(u.path): \(e.localizedDescription)"
            }
        }
    }

    public init() {}

    // MARK: - Discovery

    /// Search the user's `~/Library/Application Support/` for known
    /// Minecraft launchers and Vivecraft installations. Returns an empty
    /// array when nothing matches — the caller should fall through to a
    /// folder picker in that case.
    public func discover() -> [Candidate] {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser
        let appSupport = home.appendingPathComponent("Library/Application Support")
        var out: [Candidate] = []

        // Vanilla Mojang launcher and most third-party launchers all
        // re-extract LWJGL natives into `versions/<version>/natives-<arch>/`
        // or `versions/<version>/natives/` on each launch. The dylib must
        // land beside the LWJGL platform binaries (`liblwjgl_opengl.dylib`
        // etc) so the JVM finds it via `java.library.path`.
        let roots: [(label: String, root: URL)] = [
            ("Vanilla Launcher",
             appSupport.appendingPathComponent("minecraft/versions")),
            ("PrismLauncher",
             appSupport.appendingPathComponent("PrismLauncher/instances")),
            ("MultiMC",
             appSupport.appendingPathComponent("MultiMC/instances")),
            ("ATLauncher",
             appSupport.appendingPathComponent("ATLauncher/instances")),
            ("GDLauncher (legacy)",
             appSupport.appendingPathComponent("gdlauncher_next/instances")),
            // GDLauncher Carbon (post-2024 rewrite) buries instances one
            // level deeper: `gdlauncher_carbon/data/instances/<id>/instance/`.
            // The leading `data/` folder distinguishes it from the legacy
            // GDLauncher and the `instance/` subfolder is where mods,
            // saves, and the per-run natives live.
            ("GDLauncher Carbon",
             appSupport.appendingPathComponent("gdlauncher_carbon/data/instances")),
        ]

        for (label, root) in roots {
            guard let kids = try? fm.contentsOfDirectory(at: root,
                                                          includingPropertiesForKeys: nil) else { continue }
            for kid in kids {
                let candidates = nativesUnder(kid)
                for nat in candidates {
                    let exists = fm.fileExists(
                        atPath: nat.appendingPathComponent("libopenvr_api.dylib").path)
                    out.append(Candidate(
                        label: "\(label) · \(kid.lastPathComponent)",
                        nativesDirectory: nat,
                        alreadyInstalled: exists))
                }
            }
        }

        // Sort: Vivecraft instances first (heuristic name match), then
        // alphabetical, then "already installed" pinned to the bottom so
        // a fresh install candidate is the default action.
        return out.sorted { lhs, rhs in
            if lhs.alreadyInstalled != rhs.alreadyInstalled {
                return !lhs.alreadyInstalled
            }
            let lhsViv = lhs.label.lowercased().contains("vivecraft")
            let rhsViv = rhs.label.lowercased().contains("vivecraft")
            if lhsViv != rhsViv { return lhsViv }
            return lhs.label.localizedCaseInsensitiveCompare(rhs.label) == .orderedAscending
        }
    }

    /// Recursively look inside a launcher instance for any `natives/` or
    /// `natives-*/` directory. Stops two levels deep — that covers the
    /// `versions/<id>/natives` layout used by every launcher we've seen.
    private func nativesUnder(_ root: URL) -> [URL] {
        let fm = FileManager.default
        var hits: [URL] = []

        let scanDir: (URL) -> Void = { dir in
            // Direct: <dir>/natives, <dir>/natives-arm64, <dir>/natives-macos
            for kid in (try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [] {
                let name = kid.lastPathComponent.lowercased()
                if name == "natives" || name.hasPrefix("natives-") {
                    hits.append(kid)
                }
            }
        }

        scanDir(root)

        // versions/<id>/natives — the standard Mojang layout.
        let versions = root.appendingPathComponent("versions")
        if let vKids = try? fm.contentsOfDirectory(at: versions, includingPropertiesForKeys: nil) {
            for v in vKids {
                let nat = v.appendingPathComponent("natives")
                var isDir: ObjCBool = false
                if fm.fileExists(atPath: nat.path, isDirectory: &isDir), isDir.boolValue {
                    hits.append(nat)
                }
            }
        }

        // GDLauncher Carbon nests the actual Minecraft directory under
        // `<instance>/instance/` — scan that and its `versions/` if present.
        let nested = root.appendingPathComponent("instance")
        var nestedIsDir: ObjCBool = false
        if fm.fileExists(atPath: nested.path, isDirectory: &nestedIsDir),
           nestedIsDir.boolValue {
            scanDir(nested)
            let nestedVersions = nested.appendingPathComponent("versions")
            if let vKids = try? fm.contentsOfDirectory(at: nestedVersions, includingPropertiesForKeys: nil) {
                for v in vKids {
                    let nat = v.appendingPathComponent("natives")
                    var isDir: ObjCBool = false
                    if fm.fileExists(atPath: nat.path, isDirectory: &isDir),
                       isDir.boolValue {
                        hits.append(nat)
                    }
                }
            }
            // Vivecraft Fabric mods sometimes ship/extract OpenVR libs to
            // a custom subdir; surface them too so users can pick.
            let openvrCandidates = [
                nested.appendingPathComponent("openvr"),
                nested.appendingPathComponent("vivecraft"),
                nested.appendingPathComponent("run/natives"),
            ]
            for c in openvrCandidates {
                var d: ObjCBool = false
                if fm.fileExists(atPath: c.path, isDirectory: &d), d.boolValue {
                    hits.append(c)
                }
            }
            // If no natives folder exists at all, fall back to offering
            // the `instance/` itself so the user can drop the dylib at the
            // root — Vivecraft's Fabric loader checks the working dir as
            // a last resort.
            if hits.isEmpty {
                hits.append(nested)
            }
        }
        return hits
    }

    // MARK: - Install / uninstall

    /// Copy the bundled `libopenvr_api.dylib` into `candidate.nativesDirectory`,
    /// overwriting any pre-existing copy.
    @discardableResult
    public func install(into candidate: Candidate) throws -> URL {
        try install(into: candidate.nativesDirectory)
    }

    @discardableResult
    public func install(into nativesDirectory: URL) throws -> URL {
        guard let bundled = FuVRControlBundle.openvrShimPath() else {
            throw InstallError.shimNotBundled
        }
        let fm = FileManager.default
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: nativesDirectory.path, isDirectory: &isDir),
              isDir.boolValue else {
            throw InstallError.nativesDirectoryMissing(nativesDirectory)
        }
        let dest = nativesDirectory.appendingPathComponent("libopenvr_api.dylib")
        do {
            // Why removeItem-then-copy rather than copyItem replacing in
            // place: macOS preserves the target's quarantine xattr if we
            // overwrite, which can cause Gatekeeper to block subsequent
            // dlopens. A clean unlink+copy gets a fresh xattr set.
            if fm.fileExists(atPath: dest.path) {
                try fm.removeItem(at: dest)
            }
            try fm.copyItem(at: URL(fileURLWithPath: bundled), to: dest)
            // Strip quarantine just in case the parent dir inherited it.
            _ = try? Process.run(
                URL(fileURLWithPath: "/usr/bin/xattr"),
                arguments: ["-d", "com.apple.quarantine", dest.path]
            )
        } catch {
            throw InstallError.copyFailed(dest, error)
        }
        return dest
    }

    /// Remove a previously-installed bridge (best-effort).
    public func uninstall(from candidate: Candidate) throws {
        let dest = candidate.nativesDirectory.appendingPathComponent("libopenvr_api.dylib")
        if FileManager.default.fileExists(atPath: dest.path) {
            try FileManager.default.removeItem(at: dest)
        }
    }

    // MARK: - Mod jar install
    //
    // Vivecraft (and our fork) ships as a Forge/Fabric mod jar that has to
    // sit inside `<instance>/mods/`. The install/discovery logic mirrors
    // the dylib path — same launchers, same well-known roots — but the
    // destination is the mods folder rather than `natives/`. Discovery is
    // factored out into `discoverInstanceRoots()` so both code paths share
    // the launcher list without re-walking each `Candidate`'s natives URL
    // back up to its instance.

    /// One discovered Minecraft instance directory (the `.minecraft`-style
    /// folder where `mods/`, `saves/`, etc. live).
    public struct InstanceRoot: Equatable, Sendable, Identifiable {
        public var id: String { directory.path }
        public let label: String
        public let directory: URL
    }

    /// Walk the same launcher roots `discover()` uses, but yield the
    /// per-instance Minecraft directory (the one that owns `mods/`) rather
    /// than its `natives/` subfolder.
    public func discoverInstanceRoots() -> [InstanceRoot] {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser
        let appSupport = home.appendingPathComponent("Library/Application Support")
        var out: [InstanceRoot] = []

        let roots: [(label: String, root: URL, nestsUnderInstance: Bool)] = [
            ("Vanilla Launcher",
             appSupport.appendingPathComponent("minecraft"), false),
            ("PrismLauncher",
             appSupport.appendingPathComponent("PrismLauncher/instances"), true),
            ("MultiMC",
             appSupport.appendingPathComponent("MultiMC/instances"), true),
            ("ATLauncher",
             appSupport.appendingPathComponent("ATLauncher/instances"), false),
            ("GDLauncher (legacy)",
             appSupport.appendingPathComponent("gdlauncher_next/instances"), false),
            ("GDLauncher Carbon",
             appSupport.appendingPathComponent("gdlauncher_carbon/data/instances"), true),
        ]

        // Vanilla launcher: the AppSupport/minecraft folder itself IS the
        // .minecraft directory, no per-instance fan-out.
        let vanilla = appSupport.appendingPathComponent("minecraft")
        var vIsDir: ObjCBool = false
        if fm.fileExists(atPath: vanilla.path, isDirectory: &vIsDir), vIsDir.boolValue {
            out.append(InstanceRoot(label: "Vanilla Launcher",
                                    directory: vanilla))
        }

        for (label, root, nests) in roots where label != "Vanilla Launcher" {
            guard let kids = try? fm.contentsOfDirectory(at: root,
                                                          includingPropertiesForKeys: nil) else { continue }
            for kid in kids {
                // PrismLauncher / MultiMC / GDLauncher Carbon use
                // `<instance>/.minecraft/` (Prism, MultiMC) or
                // `<instance>/instance/` (GDLC). ATLauncher and the legacy
                // GDLauncher write directly into `<instance>/`.
                let mc: URL
                if nests {
                    let dotmc = kid.appendingPathComponent(".minecraft")
                    let nested = kid.appendingPathComponent("instance")
                    var d: ObjCBool = false
                    if fm.fileExists(atPath: dotmc.path, isDirectory: &d), d.boolValue {
                        mc = dotmc
                    } else if fm.fileExists(atPath: nested.path, isDirectory: &d), d.boolValue {
                        mc = nested
                    } else {
                        mc = kid
                    }
                } else {
                    mc = kid
                }
                out.append(InstanceRoot(label: "\(label) · \(kid.lastPathComponent)",
                                        directory: mc))
            }
        }

        return out.sorted {
            $0.label.localizedCaseInsensitiveCompare($1.label) == .orderedAscending
        }
    }

    /// Install a mod jar into a single instance's `mods/` folder, creating
    /// the folder if missing and pruning any prior `fuvr-*.jar` /
    /// `vivecraft-*.jar` to avoid double-loading two versions of the same
    /// mod (Forge/Fabric will hard-fail with a duplicate-mod-id crash).
    @discardableResult
    public func installModJar(named modJarName: String,
                              from sourceURL: URL,
                              to instanceURL: URL) throws -> URL {
        let fm = FileManager.default
        let modsDir = instanceURL.appendingPathComponent("mods")
        do {
            var isDir: ObjCBool = false
            if !fm.fileExists(atPath: modsDir.path, isDirectory: &isDir) {
                try fm.createDirectory(at: modsDir,
                                       withIntermediateDirectories: true)
            } else if !isDir.boolValue {
                throw NSError(
                    domain: "FuVR.OpenVrBridgeInstaller",
                    code: 1,
                    userInfo: [NSLocalizedDescriptionKey:
                        "Expected a directory at \(modsDir.path) but found a file."])
            }
        } catch let nsErr as NSError {
            throw NSError(
                domain: "FuVR.OpenVrBridgeInstaller",
                code: 2,
                userInfo: [
                    NSLocalizedDescriptionKey:
                        "Couldn't create mods directory at \(modsDir.path): \(nsErr.localizedDescription)",
                    NSUnderlyingErrorKey: nsErr,
                ])
        }

        // Sweep stale fuvr-/vivecraft- jars. A glob via contentsOfDirectory
        // is safer than shell expansion and stays inside the sandbox.
        if let existing = try? fm.contentsOfDirectory(at: modsDir,
                                                     includingPropertiesForKeys: nil) {
            for url in existing {
                let name = url.lastPathComponent.lowercased()
                guard name.hasSuffix(".jar"),
                      name.hasPrefix("fuvr-") || name.hasPrefix("vivecraft-")
                else { continue }
                try? fm.removeItem(at: url)
            }
        }

        let dest = modsDir.appendingPathComponent(modJarName)
        do {
            if fm.fileExists(atPath: dest.path) {
                try fm.removeItem(at: dest)
            }
            try fm.copyItem(at: sourceURL, to: dest)
        } catch let nsErr as NSError {
            throw NSError(
                domain: "FuVR.OpenVrBridgeInstaller",
                code: 3,
                userInfo: [
                    NSLocalizedDescriptionKey:
                        "Couldn't copy mod jar from \(sourceURL.path) to \(dest.path): \(nsErr.localizedDescription)",
                    NSUnderlyingErrorKey: nsErr,
                ])
        }
        return dest
    }

    /// Discover every Minecraft instance and install `fuvr-mod.jar` into
    /// each one's `mods/` folder. Per-instance failures are tolerated as
    /// long as at least one install succeeds — only an all-failures run
    /// throws.
    @discardableResult
    public func installModForAllDiscovered(modJarURL: URL) throws -> [URL] {
        let instances = discoverInstanceRoots()
        var successes: [URL] = []
        var failures: [(URL, Error)] = []

        for inst in instances {
            do {
                let dest = try installModJar(named: "fuvr-mod.jar",
                                             from: modJarURL,
                                             to: inst.directory)
                successes.append(dest)
            } catch {
                failures.append((inst.directory, error))
            }
        }

        if successes.isEmpty && !failures.isEmpty {
            // Aggregate so callers can render a single user-facing message.
            let detail = failures.map { "\($0.0.path): \($0.1.localizedDescription)" }
                .joined(separator: "\n")
            throw NSError(
                domain: "FuVR.OpenVrBridgeInstaller",
                code: 4,
                userInfo: [
                    NSLocalizedDescriptionKey:
                        "Failed to install fuvr-mod.jar into any of \(failures.count) discovered instance(s):\n\(detail)",
                ])
        }
        if !failures.isEmpty {
            // Best-effort log so partial-success runs aren't silent.
            for (url, err) in failures {
                fputs("[FuVR] mod install skipped \(url.path): \(err.localizedDescription)\n",
                      stderr)
            }
        }
        return successes
    }
}
