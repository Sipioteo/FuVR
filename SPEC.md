# MacLink — Open Source PCVR Streaming for macOS

> **Status:** Draft v0.1 — pre-implementazione  
> **Author:** Matteo (Luminos SRL / PoliTo)  
> **License (planned):** Apache 2.0  
> **Target platforms:** macOS 14+ (Apple Silicon), Meta Quest 2 / 3 / 3S / Pro

---

## 1. Obiettivo

Permettere a un Mac con Apple Silicon di renderizzare contenuti VR e streammarli in tempo reale a un visore Meta Quest connesso via **USB-C** o **Wi-Fi**, ricevendo indietro pose della testa, pose dei controller, input e (opzionalmente) hand/eye tracking. Il tutto come progetto open source mantenuto dalla community.

Il progetto **non è** un clone di Quest Link. È un'alternativa pensata per macOS, dove Meta non fornisce supporto ufficiale e dove **SteamVR non esiste**, quindi tutto lo stack è ricostruito da zero.

---

## 2. Stato dell'arte (verificato)

Cosa esiste già nello spazio adiacente:

- **ALVR** — server Windows/Linux, dipende da SteamVR per il rendering; i maintainer hanno esplicitamente escluso il supporto macOS perché SteamVR macOS è troppo vecchio e l'iniezione Vulkan layer + libunwind non sarebbe replicabile.
- **Sunshine / Moonlight** — game streaming desktop, non VR stereo.
- **Lumen** (fork Sunshine per Apple Silicon) — usa già `CGVirtualDisplay` private API + `ScreenCaptureKit` + VideoToolbox H.264/HEVC. Riferimento architetturale importante. Latenza misurata: ~1ms encode-to-network su M4 (H.264 1080p60).
- **MetalXR** — early-stage, porta OpenXR a Metal, supporta solo standalone Android headsets per limitazioni USB/DisplayPort macOS.
- **Meta XR Simulator** — runtime OpenXR ufficiale Meta che gira nativo su Mac ARM. Utile per testing in fase di sviluppo, **non sostituisce** il runtime custom necessario per streamare a un visore reale.
- **BetterDisplay / HiDPIScaler / FreeDisplay** — usano `CGVirtualDisplay` (private API ma stabile) per display virtuali.

Conclusione: i mattoni esistono, ma non li ha mai messi insieme nessuno per il caso "Mac → Quest streaming bidirezionale".

---

## 3. Architettura

```
┌─────────────────────── macOS (Apple Silicon) ───────────────────────┐
│                                                                      │
│  ┌──────────────┐    ┌─────────────────┐    ┌──────────────────┐    │
│  │  XR App      │───▶│  OpenXR Runtime │───▶│  Frame Pipeline  │    │
│  │ (Blender,    │    │   (custom)      │    │                  │    │
│  │  Godot, UE,  │    │                 │    │  Metal swap-     │    │
│  │  Unity Mac…) │◀───│  Pose / Input   │◀───│  chain capture   │    │
│  └──────────────┘    └─────────────────┘    └────────┬─────────┘    │
│                              ▲                        │              │
│                              │                        ▼              │
│                       ┌──────┴──────┐         ┌──────────────┐      │
│                       │  Transport  │◀────────│ VideoToolbox │      │
│                       │  Layer      │         │ encoder      │      │
│                       │ (TCP/UDP)   │         │ (HEVC/H.264) │      │
│                       └──────┬──────┘         └──────────────┘      │
└──────────────────────────────┼──────────────────────────────────────┘
                               │
              ┌────────────────┼────────────────┐
              │                                 │
     [USB-C / ADB tunnel]              [Wi-Fi 6 / 6E LAN]
              │                                 │
              └────────────────┬────────────────┘
                               │
┌──────────────────────────────┼──────────────────────────────────────┐
│                              ▼      Meta Quest 3 (Android NDK)      │
│                       ┌─────────────┐                                │
│                       │  Transport  │                                │
│                       │   Receiver  │                                │
│                       └──────┬──────┘                                │
│                              │                                       │
│                  ┌───────────┴─────────────┐                         │
│                  ▼                         ▼                         │
│         ┌─────────────────┐       ┌──────────────────┐               │
│         │ MediaCodec      │       │ Pose & Input     │               │
│         │ HEVC decoder    │       │ Capture (OpenXR) │               │
│         │ → AHardware-    │       │                  │               │
│         │   Buffer        │       │ Sent every frame │               │
│         └────────┬────────┘       └──────────────────┘               │
│                  │                                                   │
│                  ▼                                                   │
│         ┌─────────────────────────────────────────┐                  │
│         │  OpenXR Composition Layer (Projection)  │                  │
│         │  + Reprojection / ATW (system runtime)  │                  │
│         └─────────────────────────────────────────┘                  │
└──────────────────────────────────────────────────────────────────────┘
```

### 3.1 Componenti macOS

#### 3.1.1 Virtual Display (opzionale, fase 2)

**Strategia:** `CGVirtualDisplay` private API.

Questa API è **non documentata** ma ampiamente utilizzata da tool consolidati (BetterDisplay, DisplayLink driver, Lumen). È stabile attraverso le versioni macOS, anche se Apple può romperla. Su M4/M5 ci sono quirks documentati legati al DCP firmware (limite di 6720 pixel su pipe 0) che vanno gestiti.

**Implementazione consigliata:** subprocess helper dedicato (pattern Lumen) per evitare problemi TCC/WindowServer quando il display viene creato dal processo principale.

**Alternativa scartata:** DriverKit display extension. Apple non documenta come scrivere display extensions in DriverKit. Le poche guide esistenti riguardano USB/HID/Audio. Tentare questa strada significherebbe pionierare un'area senza documentazione né esempi.

**Alternativa secondaria:** kext legacy. Esclusa perché su Apple Silicon i kext richiedono Reduced Security mode → progetto non distribuibile a utenti normali.

**Caso d'uso primario:** "extended display VR mode" dove l'utente vede sul Quest un secondo monitor del Mac. **Non è il caso d'uso principale** del progetto e può essere posticipato.

#### 3.1.2 OpenXR Runtime

Questo è il pezzo critico. Implementazione di un runtime OpenXR 1.1 conforme che:

- Si registra come default runtime tramite `~/Library/Application Support/OpenXR/1/active_runtime.json`
- Espone `XR_KHR_metal_enable` (estensione draft, da definire formalmente con Khronos) per app native Metal
- Espone `XR_KHR_vulkan_enable2` via MoltenVK come fallback per app cross-platform
- Implementa lifecycle base: instance, system, session, swapchain, frame loop
- Gestisce `xrLocateViews` restituendo pose ricevute dal Quest, predette in avanti
- Gestisce `xrEndFrame` catturando le swapchain image, encodando, inviando
- Implementa OpenXR Action System con interaction profile `/interaction_profiles/oculus/touch_plus_controller`

**Apple non supporta OpenXR ufficialmente** (visionOS usa un'API proprietaria). Significa che le app OpenXR macOS sono poche al 2026:
- Blender 4.x ha supporto OpenXR sperimentale
- Godot Engine supporta OpenXR su macOS (senza runtime, fino ad oggi)
- Unity / Unreal hanno target macOS ma OpenXR non è esposto nativamente
- Meta XR Simulator gira su Mac ARM ma è un runtime, non un'app

Questo limita il bacino di app target del runtime. È un fatto, non un bug.

#### 3.1.3 Frame Capture & Encoding

**Capture:** dal swapchain Metal allocato dall'app target. Niente `ScreenCaptureKit` perché in modalità VR dedicata l'app rendera direttamente in textures che il runtime possiede.

**Encoding:** VideoToolbox tramite `VTCompressionSession`.

Note tecniche verificate:
- **Low-Latency Mode** (`kVTVideoEncoderSpecification_EnableLowLatencyRateControl`) introdotto in WWDC21 supporta **solo H.264**, non HEVC. Documentazione Apple ufficiale.
- Per HEVC bisogna usare modalità standard con `kVTCompressionPropertyKey_RealTime = true` + tuning manuale (no B-frames, IDR interval breve, CBR).
- Apple Silicon ha hardware encoder dedicato (Media Engine). M3+ ha encoder AV1 hardware, ma encoding AV1 da VideoToolbox **non è esposto ufficialmente** ad oggi.

**Configurazione encoder target:**
- Codec: HEVC primary, H.264 fallback per low-latency mode
- Bitrate: 100-150 Mbps per stereo 4128×2208 @ 90Hz
- GOP: solo IDR + P, no B-frames
- Latency budget: <15ms encode su M2+

**Stereo handling:** due session VideoToolbox parallele (una per occhio) oppure un singolo frame side-by-side. Lumen e Sunshine usano single frame, è la strada più semplice.

#### 3.1.4 Transport Layer

Due modalità:

**USB (preferred):** ADB reverse port forwarding.

```
adb reverse tcp:9943 tcp:9943
```

Il Quest, come device Android, espone ADB via USB. Questo crea un tunnel TCP attraverso il cavo USB. Latenza misurata: 5-10ms aggiuntivi rispetto a USB raw, throughput limitato a circa 200-300 Mbps su USB 3 (overhead ADB protocol). Sufficiente per HEVC 100 Mbps.

Vantaggi: zero driver USB custom, zero claim del device, compatibile con Developer Mode standard del Quest. È la strada usata da ALVR per il loro "wired mode".

Svantaggi: ADB aggiunge latenza e overhead rispetto a USB bulk diretto. Un'implementazione futura potrebbe esplorare Android Open Accessory (AOA) protocol, ma richiede di non usare il Quest in modalità debug standard.

**Wi-Fi:** UDP custom + Forward Error Correction.

Pattern standard ALVR-like: pacchetti UDP frammentati con sequence number, FEC Reed-Solomon per resilienza al packet loss, framing custom per delimitare frame video.

#### 3.1.5 Pose Prediction

Buffer circolare delle ultime N pose ricevute dal Quest (N=32 sufficiente). Quando l'app chiama `xrLocateViews` con `displayTime` futuro:

1. Estrai le ultime 4-8 pose
2. Calcola velocità lineare (traslazione) e angolare (quaternioni → SLERP derivative)
3. Estrapolazione lineare + smoothing (filtro complementare o Kalman)
4. Ritorna pose predetta al displayTime richiesto

Nota: il Quest stesso fa questa predizione internamente. Strategia alternativa: chiedere al Quest una pose già predetta a un timestamp futuro, e usarla as-is sul Mac. Più semplice, meno preciso se la rete jittera.

### 3.2 Componenti Quest

App Android nativa scritta principalmente in C++ (NDK) con bootstrap Kotlin/Java minimo.

#### 3.2.1 Connection Manager

Gestione connessione USB/Wi-Fi al Mac. Auto-discovery via mDNS su Wi-Fi, attesa connessione su porta nota su USB.

#### 3.2.2 Decoder Pipeline

`MediaCodec` Android in modalità asincrona, output a `AHardwareBuffer`. Quest 3 (Snapdragon XR2 Gen 2) supporta HEVC fino a 4K@90Hz hardware decode, AV1 hardware decode disponibile ma non sempre stabile per low-latency.

Output buffer → `XrSwapchain` image via `glEGLImageTargetTexture2DOES` (zero-copy GPU side).

#### 3.2.3 OpenXR Compositor

L'app Quest è un OpenXR client che:

- Crea `XrCompositionLayerProjection` con due view (left/right eye)
- Per ogni view, swapchain texture = frame decodato dal Mac
- Passa al compositor di sistema le pose con cui il Mac ha renderato il frame
- Il compositor Quest applica **ATW (Asynchronous TimeWarp)** automaticamente, riproiettando l'immagine in base alla pose attuale della testa

ATW è il meccanismo che nasconde i 20-40ms di latenza Mac→Quest. Senza ATW, motion sickness garantita.

#### 3.2.4 Pose & Input Forwarder

Loop indipendente, target 500-1000 Hz:

```
xrLocateSpace(hmd_space, predictedDisplayTime) → pose HMD
xrLocateSpace(left_controller_space, ...) → pose controller L
xrLocateSpace(right_controller_space, ...) → pose controller R
xrSyncActions() / xrGetActionStateFloat() / xrGetActionStateBoolean() → input
→ pacchetto serializzato → invio al Mac
```

Quest 3 traccia internamente a ~1000 Hz, non ha senso campionare oltre.

### 3.3 Protocollo

Protocol Buffers o **Cap'n Proto** per messaggi di controllo (preferito Cap'n Proto per zero-copy). Frame video grezzi con header binario fisso.

**Endpoint logici (canali multiplexati su un singolo socket TCP/UDP):**

| Canale | Direzione | Frequenza | Banda tipica |
|--------|-----------|-----------|--------------|
| `video` | Mac → Quest | 90 Hz | 100-150 Mbps |
| `audio` | Mac → Quest | 48 kHz | 1.5 Mbps |
| `pose` | Quest → Mac | 500-1000 Hz | ~1 Mbps |
| `input` | Quest → Mac | event-driven | trascurabile |
| `haptics` | Mac → Quest | event-driven | trascurabile |
| `control` | bidir | session events | trascurabile |

**Packet header video (proposta):**

```c
struct VideoPacketHeader {
    uint64_t frame_id;
    uint64_t timestamp_ns;        // when Mac started rendering
    PoseSnapshot rendered_pose;   // pose used by app for this frame
    uint32_t total_size;          // total bytes for this frame
    uint32_t fragment_index;      // fragmentation for UDP
    uint32_t fragment_count;
    uint16_t codec;               // HEVC / H.264 / AV1
    uint16_t flags;               // IDR, EOSPS, etc.
} __attribute__((packed));
```

Il `rendered_pose` è essenziale per ATW corretto sul Quest.

---

## 4. Stack tecnologico

| Componente | Linguaggio | Note |
|------------|-----------|------|
| OpenXR Runtime macOS | C++ 20 | Performance critical, accesso diretto Metal |
| App Mac (UI, config) | Swift / SwiftUI | Distribuzione standalone .app |
| Virtual Display Helper | Objective-C++ | Bridge a CGVirtualDisplay private |
| Transport Layer | Rust | Type safety + perf, riusabile lato Quest se serve |
| Encoder Wrapper | C++ / Objective-C++ | VideoToolbox è ObjC API |
| App Quest | C++ NDK + Kotlin | Bootstrap Android in Kotlin, core in NDK |
| Protocollo | Cap'n Proto | Zero-copy, schema-driven |
| Build | CMake + Xcode + Gradle | Cross-platform |
| CI | GitHub Actions | macOS runners + Android NDK build |

---

## 5. Roadmap & Milestone

### M0 — Spike Plan (4 settimane, pre-annuncio)

Validare le 4 ipotesi tecniche più rischiose:

1. ADB reverse over USB regge >100 Mbps in produzione con latenza <15ms?
2. VideoToolbox HEVC con `RealTime=true` produce output utilizzabile sotto i 15ms su M2/M3?
3. Un'app Quest minima riesce a ricevere stream UDP, decodare HEVC con MediaCodec, e mostrarlo come projection layer OpenXR a 90 Hz?
4. CGVirtualDisplay funziona su macOS 14, 15, 16 (Tahoe) senza modifiche significative?

**Deliverable M0:** repo privato con 4 PoC isolati, ciascuno con README + numeri misurati. Decisione go/no-go pubblica al termine.

### M1 — Proof of Life (2-3 mesi)

Streaming Mac → Quest **monodirezionale** funzionante:

- Test app macOS che renderizza una scena Metal animata
- Encoder VideoToolbox HEVC
- Transport USB via ADB
- App Quest riceve, decodifica, mostra come quad layer OpenXR (non stereo)
- Niente pose, niente input, niente OpenXR runtime macOS lato server

**Deliverable M1:** video demo, repo pubblico con licenza Apache 2.0, primo annuncio a community ALVR / r/MacOSVR.

### M2 — Bidirezionale Stereo (3-4 mesi)

- Pose tracking Quest → Mac
- Stereo rendering side-by-side
- Composition layer projection (non più quad)
- ATW funzionante
- Controller input base (button + thumbstick)

**Deliverable M2:** prima release "alpha" 0.1.0. Annuncio HN, post LinkedIn, candidatura grant NLnet.

### M3 — OpenXR Runtime macOS (4-5 mesi)

- Runtime OpenXR completo registrabile come default
- Action System completo
- Almeno una app target (Blender VR mode) gira contro il runtime
- Documentazione developer per integrare nuove app

**Deliverable M3:** release "beta" 0.2.0. Primo test con utenti esterni.

### M4 — Stabilizzazione (3-4 mesi)

- Encoder tuning, jitter reduction
- Hand tracking
- Audio bidirezionale (Mac → Quest gioco; Quest → Mac per microfono opzionale)
- Wi-Fi mode parità con USB mode
- Installer macOS firmato, app Quest su SideQuest

**Deliverable M4:** release 1.0.0. Benchmark pubblici vs Quest Link Windows.

### Totale

- Solo: ~16-19 mesi full-time per arrivare a 1.0
- Con community attiva da M2 in poi: lo stesso calendario, ma il carico personale scende dal 100% al 30-40% dopo M2

---

## 6. Stima rischi tecnici

| Rischio | Probabilità | Impatto | Mitigazione |
|---------|-------------|---------|-------------|
| Apple rompe `CGVirtualDisplay` in macOS 27+ | Media | Alto se M0 dipende dal display virtuale, **basso se non lo usiamo come pezzo core** | Architettura senza display virtuale obbligatorio; il runtime OpenXR rendera direttamente, non cattura schermo |
| ADB throughput insufficiente per 90Hz HEVC stereo | Bassa | Alto | Fallback Wi-Fi 6, downgrade a 72Hz, AOA protocol come escalation |
| VideoToolbox HEVC RealTime non scende sotto 20ms encode | Media | Medio | Fallback H.264 low-latency mode (ufficialmente supportato) |
| Meta blocca app non-Store che fanno streaming custom | Bassa | Alto | Distribuzione via SideQuest, comunità precedente (ALVR) opera così senza problemi |
| App OpenXR macOS sono troppo poche per giustificare runtime custom | Alta | Medio | Roadmap include collaborazione con Godot e Blender per testare; in alternativa fornire SDK per integrazione diretta |
| Burnout maintainer singolo | Alta | Critico | Onboarding co-maintainer da M2; struttura progetto docs-first |
| Complicazioni licenza/brevetti VR | Bassa | Alto | Apache 2.0 con clausola brevetti; nessun reverse engineering Quest Link protocol |

---

## 7. Hardware di riferimento per sviluppo

- **Mac:** MacBook Pro M2 Pro / M3 Pro o superiore. M1 base accettabile per testing ma encoder HEVC più lento.
- **Quest:** Quest 3 prioritario (target principale), Quest 2 come secondary target compatibility.
- **Cavo:** USB-C 3.2 Gen 2x1 certificato. Cavo Link ufficiale Meta consigliato ma non obbligatorio.
- **Rete (per Wi-Fi mode):** router Wi-Fi 6E dedicato, Mac collegato via Ethernet, Quest su 6 GHz.

---

## 8. Licenza & Governance

**Licenza:** Apache 2.0 con clausola brevetti esplicita. Motivazioni:
- Compatibilità con futuri contributi enterprise (Apple, Meta, Valve teoricamente possibili)
- Protezione contributor da claim brevettuali
- Permissività che massimizza adozione

**CLA:** Apache ICLA standard, amministrato via cla-assistant.io.

**Governance:** maintainer-driven, no foundation. Da M3 in poi, board informale di 3-5 maintainer con tech-lead a rotazione annuale.

**Code of Conduct:** Contributor Covenant 2.1.

---

## 9. Out of scope (esplicito)

Per evitare scope creep, queste cose **non** sono nel progetto:

- Compatibilità Quest Link / Air Link (protocollo Meta proprietario, niente reverse engineering)
- Supporto SteamVR (non gira su Apple Silicon nativamente, irrelevante)
- Supporto a visori non-Quest (PSVR2, Pico, HTC) — accettiamo PR ma non target principale
- Supporto Windows / Linux server-side (Mac-only by design)
- Apple Vision Pro come client (Vision Pro ha API native diverse, sarebbe progetto separato)
- DRM / protected content
- Cloud streaming / remote streaming over Internet (LAN only nella v1)

---

## 10. Riferimenti tecnici

- OpenXR 1.1 specification: https://registry.khronos.org/OpenXR/
- VideoToolbox WWDC21 low-latency: https://developer.apple.com/videos/play/wwdc2021/10158/
- ALVR wired setup wiki: https://github.com/alvr-org/ALVR/wiki/ALVR-wired-setup-(ALVR-over-USB)
- Lumen (riferimento Apple Silicon): https://github.com/trollzem/Lumen
- MetalXR (riferimento OpenXR Metal): https://github.com/PeaPodDevs/MetalXR
- Meta XR Simulator (riferimento Mac ARM OpenXR): https://developers.meta.com/horizon/downloads/package/meta-xr-simulator-mac-arm/
- CGVirtualDisplay headers reverse-engineered: https://github.com/w0lfschild/macOS_headers
- Meta Quest OpenXR docs: https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/

---

## 11. Domande aperte

Punti su cui serve decidere prima di committarsi pubblicamente:

1. **Nome del progetto.** "MacLink" è semantica troppo vicina a Quest Link e potrebbe creare confusione legale. Alternative da valutare: Lumen è già usato (e c'è anche il tuo Lucerna), serve qualcosa di distinto.
2. **Estensione OpenXR Metal.** Esiste come draft? Va proposta a Khronos? In assenza, l'unica opzione è Vulkan via MoltenVK, che aggiunge un layer di traduzione.
3. **Single-process vs daemon.** Il runtime OpenXR deve girare nel processo dell'app o in un daemon separato? Convenzione OpenXR è in-process, ma il streaming long-running suggerisce daemon.
4. **Distribuzione.** Mac App Store è incompatibile con private API. Notarized .app distributed off-store è la strada. Per il Quest: Meta Horizon Store rifiuterà; via SideQuest e App Lab (oggi Horizon Lab) è realistico.
5. **Coinvolgimento accademico.** Il progetto ha valore di ricerca: pubblicabile come paper systems/HCI, possibile inquadramento come tesi PhD al DAUIN. Decidere se pubblicare prima paper o codice.

---

*Documento vivo. Modifiche tracciate via git, discussioni aperte come issue GitHub una volta reso pubblico il repo.*
