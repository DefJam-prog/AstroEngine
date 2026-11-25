# Журнал решений

### D217 — capability-gated sRGB Scene/Game targets (07.2ce)

For BGFX targets 1 and 2, use mutable-sRGB BGRA8 attachments only when runtime caps advertise both 2D sRGB texture and framebuffer support. Every producer for the attachment outputs linear RGB, including 2D sprite image samples and tint; alpha remains linear. Let the attachment perform output encoding and linear-light fixed-function blending. Keep the Editor swapchain unchanged and sample the already-encoded target without sRGB decoding in ImGui. If capability is absent, preserve the legacy target/output path and emit a diagnostic. This is verified only on Windows x64/D3D11. No project schema or authored asset changes. Production-shader smoke measured expected output bytes; the 07.2cf no-light Scene/Game viewport crop now matches attachment readback, while general color/alpha cases and other backends remain unverified. See [07.2ce report](reports/07.2ce-linear-light-srgb-attachments.md) and [07.2cf report](reports/07.2cf-framebuffer-readback-and-smoke-input.md).

### D218 — readback framebuffer и детерминированный ввод Editor smoke (07.2cf)

Проверять Scene/Game composition в Windows smoke через штатный BGFX pipeline: blit BGRA8 sRGB attachment в отдельную `BGFX_TEXTURE_READ_BACK` texture, асинхронный `bgfx::read`, затем сравнение внутренних точек screenshot с CPU копией. Условие: не менее 99,5% выборок в пределах 8/255; реальные D3D11 выборки совпали точно. Не включать readback в обычный кадр и не распространять вывод на другие backends. Синтетический drag input подавать в ImGui IO после SDL backend polling и до `ImGui::NewFrame`, чтобы тест не зависел от ОС-фокуса и не перезаписывался backend. Это меняет только test harness, не пользовательскую маршрутизацию событий/API. См. [отчёт](reports/07.2cf-framebuffer-readback-and-smoke-input.md).

### D219 — case-specific BGFX viewport readback matrix (07.2cg)

Расширить smoke readback на те view, которые соответствуют конкретной fixture: Scene View alpha composition для общего Editor BLEND и равного MASK cutoff; Game View для Runtime texture, MASK и mip sampler cases. Не считать нерелевантную соседнюю view основанием теста. D3D11 acceptance сейчас эмпирический: 8/255 RGB channel delta, 99,5% для opaque/Game/no-light и 98,5% для Scene alpha; сохранять реальные число совпавших точек и max delta. Измеренные outlier samples MASK/BLEND не удалять из расчёта и не заявлять как golden. Пересматривать допуски отдельно на каждом реально доступном backend до общего обещания parity. Изменение остаётся в Editor smoke/CMake harness без API, schema или dependency changes. См. [07.2cg отчёт](reports/07.2cg-framebuffer-material-alpha-matrix.md).

### D220 — граница доступности альтернативного BGFX backend (07.2ch)

Не считать наличие Vulkan loader/драйвера доказательством доступности альтернативного backend в Editor. В текущей конфигурации `bgfx::Init` жёстко фиксирует D3D11 с `fallback=false`; Scene View шейдеры создаются только в профиле Windows `s_5_0`. BGFX `config.h` включает Vulkan по умолчанию для Windows, но не подтверждает, что приложение может выбрать этот renderer и загрузить подходящие shaders. 07.2ch поэтому остаётся assessment-only. Для Vulkan smoke потребуются явная ограниченная renderer-selection точка и соответствующие shader artifacts; D3D11 остаётся default. До этого cross-backend parity не заявлять. См. [07.2ch report](reports/07.2ch-alternate-bgfx-backend-assessment.md).

### D221 — изолированный BGFX Vulkan startup smoke (07.2ci)

Сохранить D3D11 штатным renderer-ом Editor. Добавить только диагностические аргументы `--smoke-bgfx-backend` (проверяет implicit default D3D11) и `--smoke-bgfx-vulkan` (явный Vulkan, fallback выключен); не вводить пользовательскую настройку renderer. Собирать локальные SPIR-V variants для Scene View, BGFX ImGui и используемых model-preview vertex shaders через уже подготовленный BGFX shaderc. Vulkan smoke должен подтвердить фактическое имя renderer, создание BGFX programs и UI draw calls; если связанная библиотека вообще не перечисляет Vulkan, CTest может штатно skip с кодом 77. На текущем ПК оба теста проходят. Это подтверждает только Vulkan startup/initialization и shader-program creation: Vulkan 3D fixture, framebuffer readback, визуальная эквивалентность, производительность и другие платформы не проверены. См. [07.2ci report](reports/07.2ci-bgfx-vulkan-startup-smoke.md).

### D216 — BGFX alpha blending and viewport presentation assessment (07.2cd)

The current 3D Scene/Game BLEND path is gamma-space: shaders output sRGB-encoded RGB and blend into ordinary BGRA8. The opaque target is passed through ImGui without a second color transform, so presentation does not repair internal blend error. For a bounded D3D11 prototype, evaluate a mutable-sRGB BGRA8 attachment gated on runtime texture/framebuffer caps. All producers of that target, including 2D sprite texture/tint, must emit linear RGB; alpha remains linear. Keep the editor swap chain unchanged. This is an implementation recommendation, not a cross-backend contract; no runtime/API/schema changed in 07.2cd. See [assessment](reports/07.2cd-alpha-blend-target-assessment.md) and local BGFX definitions `build/spike-deps/bgfx/include/bgfx/defines.h`.

### D215 — BGFX base-color transfer owner and output encoding (07.2cc)

Keep base-color role semantics in existing material/import contracts. The CPU path decodes source sRGB RGB, applies linear `baseColorFactor` once, keeps alpha linear, and creates linear-space mip data uploaded as ordinary BGRA8. BGFX lit shaders consume this linear value without a second decode; unlit texture and vertex-color shaders encode output RGB once, and lit shaders encode after linear Lambert multiplication. Runtime BGFX uses a white shader factor for textured draws whose texture already contains the factor; vertex-color draws retain their shader multiplier. No material schema/API change. Verified on Windows x64/D3D11 by a production-shader screenshot smoke with measured values 93/66/128/66 (±3 accepted). Limit: fixed-function alpha blending on ordinary BGRA8 is not established as linear-light correct; neither Editor composition nor other GPU backends have numeric golden coverage. See [07.2cc](reports/07.2cc-bgfx-color-transfer.md) and [Khronos glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness).

### D214 — semantics material color vs renderer transfer contract (07.2cb)

Сохранять color semantics на уровне ролей ресурса/material schema: `baseColorFactor`/`emissiveFactor` и `COLOR_0` linear; base-color/emissive texture RGB sRGB encoded, texture alpha linear; normal/metallic-roughness/occlusion data linear. Не добавлять отдельное общее `ColorSpace` поле в `.astro-material`, пока семантика однозначно следует из role. При этом каждый renderer adapter обязан иметь явный end-to-end контракт для decode, linear factor/filter/light operations и output encode; backend detail не должен менять значение сохранённого материала. Оценка кода выявила текущую непоследовательность BGFX: CPU готовит linear BGRA8 + factor и загружает как ordinary BGRA8, а lit textured shader повторно вызывает sRGB decode; no-light path напрямую передаёт значение, offscreen targets тоже BGRA8. Это code-path finding, но не отдельный visual numeric GPU test. До дальнейшего расширения материалов сначала исправить путь и добавить mid-gray/non-white-factor проверки. Scope 07.2cb — документация/assessment, без runtime изменения. Основание: [07.2cb](reports/07.2cb-material-color-space-contract.md) и Khronos [glTF 2.0 material semantics](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness).

### D213 — Scene View lighting equivalence и front/back smoke (07.2ca)

Scene View target получает тот же project-owned Lambert shader set, normal-matrix uniform и directional-light uniforms, что и предусмотренный renderer path; это wiring подтверждается отдельным D3D11 smoke. Smoke рендерит неизменную тестовую пару плоскостей в трёх режимах: красное направленное освещение, legacy unlit shader без enabled света и Lambert shader с нулём источников. Raw crop только Scene View сравнивается побайтно через SHA-256; ожидается совпадение двух no-light режимов. Front/back regions в изолированной тестовой сцене проверяют положительное число красных пикселей на освещаемой поверхности и ноль на поверхности с обратной нормалью. Для текущего Scene View shader sampled base-color texture RGB декодируется из sRGB, vertex-color RGB интерпретируется как linear, Lambert arithmetic выполняется в linear, результат lit RGB кодируется в sRGB; в Scene View material factors предварительно применяются к кэшированной base-color текстуре. Это фиксирует наблюдаемую реализацию, но не утверждает общий контракт между backends, точность цветовых профилей или PBR. Smoke ограничен Windows x64 / BGFX D3D11; новые зависимости и Scene/file API не добавлены. Основание: [отчёт 07.2ca](reports/07.2ca-scene-view-lighting-equivalence.md).

### D212 — Scene View directional diffuse (07.2bz)

Подключить ограниченное Lambert-освещение к существующему Scene View renderer, сохранив его текущий материал/alpha/fallback pipeline. Добавить authored normals к vertex layout, использовать текущий inverse-transpose helper и до четырёх enabled `DirectionalLight3D` в стабильном EntityId порядке. При отсутствии света выбирать исходные unlit программы, чтобы обычные сцены сохраняли прежний render path. Project preview не менять. Smoke подтверждает направленно окрашенные пиксели Scene View и Runtime Game View; он не сравнивает captured no-light изображение с pre-light baseline и не измеряет отдельную пару противоположных нормалей. Следующий тестовый шаг должен добавить эти сравнения. Scope — Windows BGFX/D3D11; PBR, тени и перенос политики цветового пространства между backends не решены. См. [07.2bz](reports/07.2bz-scene-view-directional-diffuse.md).

### D211 — Bounded BGFX directional diffuse in Game View (07.2by)

Implement the 07.2bx proposal as a limited Runtime/Game View pass: project-owned shader sources; inverse-transpose 3x3 world normal matrix; at most four directional lights in existing stable EntityId order; sRGB decode for texture RGB, linear interpretation of vertex colors and baseColorFactor, linear Lambert multiplication, and sRGB encoding for lit output. A scene with no usable light and a mesh without normals remain visible through the unlit branch. The Editor reports missing NORMAL and excess-light cases. Scene View and Project model preview retain their existing unlit programs because their renderer path is separate. This implementation does not complete D210's requested explicit no-light image-diff or opposite-facing pixel-pair assertion; the smoke only proves a red-lit image and regressions separately prove that the no-light Game View still renders. Follow-up must strengthen pixel comparisons and consider wiring Scene View before claiming general lighting. Scope stays Windows D3D11; no PBR, specular, ambient, shadows, or new dependency. See [07.2by](reports/07.2by-bgfx-directional-diffuse.md).

### D210 — Ограниченный BGFX Lambert lighting vertical slice перед PBR (07.2bx)

Не подключать существующий BGFX renderer к свету без передачи и корректного преобразования нормалей. Следующая implementation slice должна использовать собственные исходники vertex/fragment shaders; считать нормаль inverse-transpose матрицей из world transform; ограничить shader directional lights фиксированным числом и сообщать о превышении; выполнять diffuse arithmetic в linear space и явно кодировать final output согласно формату render target. Для мешей без authored normals сохранить видимый unlit fallback с диагностикой; не придумывать нормаль. Acceptance: Editor Game View pixel smoke сравнивает сцену без enabled lights с прежним unlit baseline, проверяет направленное освещение фронтальных/обратных поверхностей и сохраняет текущие OPAQUE/MASK texture/fallback checks. Это не PBR, не specular, не environment lighting и не shadows. Проверка на текущем Windows x64 D3D11 backend; другие платформы/backends вне этого первого визуального прототипа. Основание: [07.2bx](reports/07.2bx-bgfx-lighting-assessment.md).

### D209 — DirectionalLight3D authoring data and packet contract (07.2bw)

`DirectionalLight3D` stores enabled state, linear RGB `[0,1]`, and a unitless intensity multiplier `[0,64]`; it does not claim a physical unit. The source direction is local `-Z`, composed through hierarchical rotations only, ignoring translation and scale. Scene v12 adds the record while v1–v11 remain readable without implicit rewrite. The backend-neutral packet sorts enabled lights by stable entity ID. This decision adds no shader consumption or visual-lighting promise; renderer integration requires a separate shader/material assessment and visual tests.

### D208 — Сначала ввести явный directional-light data contract (07.2bv)

Не добавлять скрытое освещение по умолчанию и не подменять отсутствие источников фиктивным Light. Первый lighting implementation должен вводить версионированный `DirectionalLight3D` с typed Scene API/commands, Transform-derived world direction без влияния scale и immutable light snapshot в backend-neutral render packet. Scene v11 продолжает читаться без миграции на диск; сцены без authored enabled light сохраняют текущий unlit output. На этом шаге не включать PBR, point/spot lights или shadows. Сначала покрыть command validation/undo, Scene v11/v12 round-trip/migration, hierarchy direction, invalid bounds и render packet; GPU shader нужен только в последующем визуальном vertical slice. Основание и риски: [07.2bv](reports/07.2bv-lighting-vertical-slice-assessment.md).

### D207 — Поддержать только alpha-test MASK в Runtime BGFX на этом этапе (07.2bu)

Разрешить в Runtime BGFX OPAQUE и MASK base-color materials, передав alpha mode/cutoff в существующий shader uniform и сохранив запись глубины для MASK. Mip levels для MASK готовить alpha-coverage aware генератором, а alpha mode/cutoff включать в texture cache identity. BLEND требует отдельной сортировки/state policy и пока остаётся на whole-pass Editor fallback; другие PBR/UV combinations тоже fallback. Это ограниченный Windows D3D11 prototype, не общий PBR renderer. Проверка: новый alpha checker Play smoke и полный CTest. См. [07.2bu](reports/07.2bu-runtime-bgfx-mask.md).

### D206 — Проверять все четыре Runtime BGFX mip min-фильтра в Game View (07.2bt)

Для четырёх glTF mip min-filter modes сохраняются отдельные интеграционные Game View smoke на детерминированной уменьшенной checker-текстуре. Проверяется выбранный enum и измеряемый диапазон цветных пикселей на Windows D3D11; pixel counts не являются переносимыми golden для иных GPU/backend. Upload и fallback поведение остаются в прежних тестах. Первый полный прогон единично уронил `editor.asset_model_preview.smoke`, затем он прошёл 10/10 повторов и следующий полный CTest. Код не менять и assertions не ослаблять на основании единичного результата; при повторении исследовать host/window input timing. См. [отчёт 07.2bt](reports/07.2bt-runtime-bgfx-mipmap-comparison.md).

### D205 — переиспользовать общий CPU mip generator в opaque Runtime BGFX texture path (07.2bs)

Runtime BGFX создаёт mipmapped texture и передаёт каждый CPU chain level отдельным `updateTexture2D`; total chain bytes учитываются в общем 256 MiB cache budget. Один cache key различает цепочку mip и single-level upload; sampler flags берутся из общей filter-selection policy. Scope ограничен opaque base-color: alpha MASK/BLEND, PBR maps и UV extensions остаются полным fallback. Поскольку BGFX `updateTexture2D` возвращает `void`, renderer проверяет до постановки upload валидность размеров/уровней и handle allocation, но не может сообщить последующую асинхронную ошибку конкретного уровня.

### D204 — перед реализацией Runtime BGFX mip отдельно проверить полный upload/lifetime path (07.2br assessment)

Уже существующий CPU mip generator и alpha-aware policy переиспользуются; вторую реализацию фильтра создавать не следует. Adapter должен создавать и загружать все mip levels, считать полный chain в 256 MiB cache budget, завершать/очищать texture handles при отказе любого уровня и отклонять весь Runtime pass до draw. Первый implementation scope — opaque base-color с четырьмя mip minification modes. Alpha MASK/BLEND, прочие PBR карты и UV extensions остаются fallback до отдельных решений и тестов. Сигнатуры и возможности BGFX сверяются с фактически используемой локальной версией.

### D203 — передавать source glTF base-color image в Runtime adapter только как проверенный host path (07.2bq)

Scene/render packet не декодирует файлы и не открывает произвольные пути. Importer уже подтверждает целостность glTF и внешних image через content hash; packet переносит source path/hash и sampler values только если проектный material override отсутствует. Editor host canonicalizes path и разрешает WIC decode только внутри `Assets`; optional BGFX adapter использует путь/hash/factor cache key и принимает лишь тот же opaque UV0/non-mip subset, что project textures. Любая ошибка отклоняет полный adapter pass, сохраняя Editor renderer. Формат Scene и serialization не меняются.

### D202 — разрешить в Runtime BGFX только ограниченный opaque base-color texture subset (07.2bp)

Runtime BGFX адаптер не получает доступ к project filesystem напрямую. Editor host резолвит зарегистрированный `AssetId`, передаёт актуальный AssetRegistry SHA и декодированный WIC BGRA8. Adapter применяет linear color-space/factor, upload/cache и sampler flags; cache ограничен 256 MiB и keyed по AssetId + SHA + factor. Первый subset ограничен opaque, UV0, identity transform, non-mip nearest/linear filters и repeat/mirror/clamp. Остальные texture/PBR/alpha paths отклоняются до draws, чтобы Game View целиком использовал прежний Editor renderer. Изменён BGFX view order: каждый target выполняет clear до соответствующего 3D pass, иначе Game View clear затирал бы Runtime 3D output перед ImGui compositing. Smoke требует пиксельный результат, а не только counters.

### D201 — регрессировать оба исхода Game View adapter selection (07.2bo)

Успешный Runtime BGFX submission и fallback на старый Editor renderer проверяются раздельными Editor Play smoke. Текстурный путь подтверждает fallback через отсутствие adapter submissions и наличие textured mesh submissions Editor renderer. Тест не требует, чтобы adapter частично рисовал сцену до fallback: renderer должен владеть всем 3D pass. Это сохраняет нынешний узкий adapter contract до отдельной реализации и проверки texture/alpha support.

### D200 — подключать Runtime BGFX adapter к Game View только во время Play (07.2bn)

Editor сохраняет владение SDL window, BGFX context, shader program и Game View framebuffer; дополнительный BGFX context не создаётся. При активной RuntimeSession Game View строит backend-neutral packet из runtime Scene и передаёт его adapter. Adapter выбирается только при чистом packet и успешной проверке целого набора материалов. Если packet или adapter не поддерживает сцену (например, texture/alpha path), весь 3D draw pass остаётся на прежнем Editor renderer. Настройка включается только вместе с `ASTRO_BUILD_RUNTIME_BGFX_3D`; обычный Editor build не получает новую runtime-зависимость.

### D199 — smoke BGFX через disposable RuntimeSession Play (07.2bm)

Опциональный BGFX packet smoke запускает `RuntimeSession` на authoring Scene и строит render packet из её runtime-копии перед каждым кадром. Проверяется fixed step, отличие runtime Scene от authoring Scene, неизменность исходного Transform и остановка сессии до завершения host. Это доказывает Play-to-adapter handoff для sample, но не интеграцию с Editor/Game View: BGFX init/window/program остаются у sample host, adapter включается отдельно и проверен на Windows x64.

### D198 — выделить опциональный BGFX Runtime consumer для Scene packet (07.2bl)

`astro_runtime_bgfx_scene3d` остаётся отдельным Windows x64 adapter и включается явным `ASTRO_BUILD_RUNTIME_BGFX_3D=ON`; BGFX init/window/program принадлежат host, а adapter отвечает за render-item submissions и cache GPU geometry. Opaque baseColorFactor входит в ключ cache, чтобы разные authored цвета получали правильные vertex buffers. Texture/alpha/PBR passes явно отклоняются. Cache ограничен 4096 вариантами; geometry следует валидированным MeshData bounds и использует 32-bit indices только при поддержке backend. Smoke подтверждает packet/camera/world transforms/draw/readback на BGFX D3D11, но не общий Play lifecycle и не переносимость backend.

### D197 — передавать Scene в backend-neutral 3D render packet (07.2bk)

`astro_scene_render3d` формирует immutable-by-convention snapshot Scene для последующего renderer backend: Camera3D, world transforms, shared immutable `MeshData`, component IDs, optional validated `MaterialAssetData`, stable AssetId и per-entity diagnostics. Primitive геометрия генерируется без файлов; MeshRenderer поставляет optional importer callback, поэтому core packet не зависит от fastgltf, BGFX, SDL или OS decoder. Scene material override разрешается через optional AssetRegistry и имеет приоритет над source material. Renderable с отсутствующим geometry/material или плохим Transform опускается с diagnostic; отсутствие/ошибка active Camera3D делает build неуспешным. Packet не является GPU ресурсным cache и должен строиться/обновляться на границе Scene изменений, а не считаться renderer.

### D196 — не считать BGFX preview готовым Runtime 3D renderer (07.2bj)

Опциональный `astro_render3d_bgfx_smoke` остаётся backend/sample spike: он Windows/MSVC-only, загружает одну `MeshRenderer`, рисует заданную geometry в origin и покрывает материал ограниченным baseColor diagnostic. Он не исполняет Scene целиком, не учитывает hierarchy/Camera3D и не покрывает `PrimitiveRenderer`. Editor `SceneViewTarget` приватен в `editor/main.cpp`; переносить его напрямую в Runtime нельзя из-за зависимости и lifecycle Editor. До standalone 3D Play нужно выделить backend-neutral Scene render-item preparation (camera, world transforms, mesh/primitive geometry, material refs, diagnostics), а renderer/backend оставить заменяемым и необязательным. Никаких API/runtime code changes этим решением не вводится.

### D195 — различать materialized primitive при проверке Scene/Game View (07.2bi)

Smoke считает отдельные material draw submissions только для `PrimitiveRenderer` с непустым project material override, отдельно в Scene View и Game View. Проверка Game View требует активную Camera3D. Screenshot threshold использует жёлтый base color, отличимый от основных red/green/blue 2D smoke markers; тем самым тест не может пройти лишь из-за чужой красной sprite-плашки. Проверка пороговая и зависит от Windows D3D11/драйвера, не объявляется exact golden.

### D194 — проверять проектный материал примитива через Scene View pixel capture (07.2bh)

Материал встроенного примитива проверяется на видимом Cube в Editor Scene View, а не только через persisted AssetId. Изначальный smoke требовал BGFX submissions, красный пиксельный порог и полный TGA кадр. В 07.2bi matcher заменён уникальным жёлтым оттенком после обнаружения совпадающего красного 2D marker; дополнительно проверяются submissions для Scene View и Game View. Порог вместо точного golden не фиксирует backend/GPU-зависимую картинку.

### D193 — проектные материалы для встроенных примитивов (07.2bg)

`PrimitiveRenderer` получает optional `materialOverride` и schema v2. Отдельная `entity.set_primitive_material_override` v1 сохраняет совместимость существующей команды формы/видимости. Scene v11 пишет AssetId (0 = нет override); Scene v10 v1 мигрирует к пустой ссылке без изменения исходного файла до явного Save. Inspector предлагает только зарегистрированные материалы проекта и разрешает их до изменения Scene. Рендер повторно использует текущий BGFX материалный путь; новая зависимость не добавляется. Внешние typed-клиенты отвечают за проверку AssetId по registry.

### D192 — PrimitiveRenderer авторится через Inspector и typed command (07.2bf)

Для существующего встроенного примитива в Inspector открыты только два persisted поля компонента: kind (Cube/Plane/Sphere/Cylinder) и visible. Изменения объединяются в `entity.set_primitive_renderer` v1 и остаются одной undoable транзакцией; общий Transform продолжает отвечать за положение, поворот и размер. Не добавлять отдельный primitive material/color schema до конкретного требования/рендер-контракта. Полный Release CTest: 96 passed, 1 штатно skipped из 97. Inspector smoke проверяет отрисовку контролов, command tests — изменение/Undo/Redo; физические GUI clicks и pixel golden не проверены. См. [отчёт](reports/07.2bf-primitive-inspector-authoring.md), [TESTING](TESTING.md#072bf-параметры-primitive-renderer-в-inspector).

### D190 — встроенная геометрия примитивов сцены (07.2bd)

Встроенные Cube/Plane/Sphere/Cylinder — значения `PrimitiveRenderer` в Scene document, а не автоматически записанные project assets. Geometry factory строит ограниченные indexed mesh buffers; общий Editor BGFX Scene/Game View кэширует и рисует их. Это сохраняет пользовательские Assets неизменными и не требует генерации файлов/AssetId. MeshRenderer и PrimitiveRenderer взаимоисключающие. Scene schema повышена до v10 с чтением v1–v9 и миграцией только при explicit save. На этом шаге добавлены геометрия и Editor draw path; typed command/Undo, полноценные материалы, runtime renderer и отдельный pixel smoke остаются незавершёнными. См. [07.2bd](reports/07.2bd-scene-primitives.md).

### D189 — системный выбор файла проекта и папки (07.2bc)

Для кнопки «Обзор…» использовать уже подключённые SDL3 `SDL_ShowOpenFileDialog` и `SDL_ShowOpenFolderDialog`, без нового GUI/диалогового пакета. Открытие фильтруется по `.astro`; в создании выбирается существующий родительский каталог, а отдельное имя проекта образует новую дочернюю папку. Имя должно быть одним компонентом пути, существующие папки не перезаписываются. Результат асинхронного callback применять в главном цикле; cancel не меняет введённый текст. Проверено вручную на Windows x64 при открытии/отмене диалогов, а соединение parent/name покрыто unit-тестом. Создание через нативный picker, другие платформы и длинные пути вручную не проверялись. [Отчёт](reports/07.2bc-project-path-browse.md), [TESTING](TESTING.md#072bc--выбор-пути-проекта).

### D188 — захват drag в Scene View до отпускания (07.2bb)

В Rendered Scene View правый orbit и средний pan начинаются только при нажатии внутри viewport и остаются активными до отпускания соответствующей кнопки, включая движение вне viewport. Press вне области не начинает жест; колесо остаётся ограничено hover. Это временное состояние редакторской камеры, не Scene mutation. Расширяем принятое для Project preview поведение на основную сцену. Интеграционный smoke должен проверять SDL input events через штатный ImGui backend, camera delta за пределом viewport, действительные внешние press и release. Не считать синтетические события проверкой физической мыши. [Отчёт](reports/07.2bb-scene-view-drag-capture.md), [тест](TESTING.md#072bb--захват-drag-в-scene-view).

### D187 — удерживать Project preview drag до отпускания (07.2ba)

Правый orbit и средний pan выбранной модели начинаются только при press внутри видимого image, но после старта следуют за движением курсора вне image, пока кнопка остаётся down. `MouseDown=false` сбрасывает каждый capture; смена выбранного asset/root сбрасывает оба явно. Колесо остаётся ограничено hover. SDL integration smoke должен доказать выход из rect, изменение yaw/focus, игнорирование drag, начатого снаружи, и корректный release. Решение ограничено Project preview; Scene View input не расширяется в этом подэтапе. Физический input не заявляется. [Отчёт](reports/07.2ba-project-preview-drag-capture.md), [тест](TESTING.md#072ba--захват-drag-в-project-model-preview).

### D186 — тестировать GUI-жесты через штатные SDL events (07.2az)

Текущий Computer Use API не удерживает правую/среднюю кнопку при drag, поэтому автоматизировать physical input здесь нельзя. Чтобы проверить wiring, Editor smoke отправляет в своё SDL окно отдельные motion/down/motion/up events для right и middle buttons и проходит стандартный `ImGui_ImplSDL3_ProcessEvent` плюс viewport callbacks. Принимать smoke только при изменении yaw и focus. Не заявлять подтверждённую работу физической мыши или OS SendInput; ручная проверка остаётся полезной. [Отчёт](reports/07.2az-project-preview-mouse-input.md), [тестирование](TESTING.md#072az--sdl-события-мыши-project-preview).

### D185 — повторно показывать Project model preview после resize (07.2ay)

При изменении ширины/высоты Project окна сбрасывать состояние однократного scroll-to-preview, чтобы повторная прокрутка вернула выбранное изображение в видимую область. Проверять геометрию изображения против фактического ImGui clip rect в компактном 864×608 smoke; не требовать точного pixel golden. Смена размера не меняет AssetId, модельные источники, сцену или камеру. Физический right/middle input и платформы кроме Windows D3D11 этим решением не подтверждаются. [Отчёт](reports/07.2ay-project-preview-resize.md), [проверки](TESTING.md#072ay--project-model-preview-после-resize).

### D184 — границы ручной проверки Project model preview (07.2ax)

Оценивать Project model viewport только на изолированной glTF-копии: текущий importer не поддерживает доступные пользовательские OBJ/FBX, поэтому их не конвертировать и не менять для этой проверки. Зафиксировать, что wheel input проверен реально, 4:3 viewport отображает модель, а автоматизация не может выдавать right/middle drag. До проверки человеком не считать orbit/pan UI gestures подтверждёнными. В текущем окружении остался вопрос layout: при 864×608 preview уходит ниже видимой части Project panel. Не менять размеры экрана/OS и не расширять формат импорта в рамках наблюдательного подэтапа. См. [отчёт](reports/07.2ax-manual-model-preview-review.md) и [тесты](TESTING.md#072ax--manual-project-preview-review).

### D183 — автоматическая проверка Project model viewport (07.2aw)

Расширить существующий Editor smoke сценарий для модели только во временном smoke project: добавить вторую glTF primitive и проверять две BGFX submissions. После однократного Frame All программно вызывать `SceneViewTarget` orbit/pan/zoom entry points, общие с GUI handlers; считать проверку успешной лишь при изменении yaw, focus и distance. Так проверяется реальная сцена viewport и маршрутизация контроллера без нестабильной симуляции OS mouse events. Тест не мутирует tracked fixtures/user assets и не заявляет ручную проверку ввода или другие graphics backends. Smoke crop остаётся coarse foreground-count assertion. См. [архитектуру](ARCHITECTURE.md#проверка-управления-камерой-model-preview-072aw), [тестирование](TESTING.md#072aw--orbitpanzoom-и-модель-с-несколькими-primitives) и [отчёт](reports/07.2aw-model-preview-controls.md).

### D182 — изолированный 3D preview модели (07.2av)

Для выбранной зарегистрированной glTF-модели Project создаёт временную Scene и переиспользует `SceneViewTarget`, `resolveMeshRenderer`, source-material pipeline и локальную `SceneViewCamera3D`, но в отдельных BGFX view IDs 5/7 и самостоятельном framebuffer/cache. Так preview не использует рабочую сцену, активную Game View camera и caches Scene/Game. При выборе модели применяются Frame All bounds; right orbit, middle pan и wheel zoom меняют только preview camera. Смена выбранного asset/project освобождает GPU mesh/material cache. Вход ограничен 128 primitives, 64 MiB геометрии; существуют и другие границы renderer. Preview может вызвать запись производного glTF cache под Library, но не изменяет Assets или сцены. Windows D3D11 smoke проверяет submissions, framing, diagnostics и экранные пиксели. Не проверены физический mouse input, другие backend/OS, skeletal/animation/lighting preview; поддержки произвольного 3D формата нет. Результаты в [отчёте](reports/07.2av-project-model-viewport.md).

### D181 — предпросмотр изображений и геометрии моделей (07.2au)

Project показывает зарегистрированное image из проверенного BGFX/WIC cache, используя только `AssetRegistry::cachedPath`; зарегистрированная glTF model разрешается importer по её registry-owned path и даёт метрики mesh/primitive/vertex/index. Сводка модели строится при выборе и остаётся до смены AssetId. Источники и Scene read-only; glTF importer может создать/переиспользовать производный Library cache. Выбрана структурная модельная сводка вместо второго BGFX viewport, поскольку существующий renderer targets привязаны к Scene/Game View lifecycle. Это не визуальный 3D просмотр, не material/script preview и не общий файловый browser. Проверено focused Release 5/5; полный результат в [отчёте](reports/07.2au-asset-previews.md).

### D180 — read-only Asset Browser на основе AssetRegistry (07.2at)

Первый каталог ресурсов Editor строится из уже проверенных in-memory maps `AssetRegistry` (image/model/material/script), а не путём обхода произвольной файловой системы. Записи объединяются и сортируются по стабильному AssetId; выбор отображает тип и registry-relative source path через существующую Project selection. Поиск — точная UTF-8 подстрока пути или десятичного ID, фильтр — один из четырёх типов. List clipping ограничивает число одновременно созданных ImGui rows; каталог кешируется в Editor и перестраивается при смене проекта, изменении количества записей или явном Refresh. Refresh перечитывает текущий in-memory registry, а не внешний index. Источники и cache не изменяются. Поиск регистрозависим; дерево файлов, неимпортированные файлы, previews, open/drag-and-drop, сортировки/группировки UI и синхронизация внешних изменений не входят. Windows x64/D3D11: focused 4/4, full Release CTest 95/95. См. [архитектуру](ARCHITECTURE.md#read-only-asset-browser-072at), [UX](EDITOR_UX.md#asset-browser-072at), [тесты](TESTING.md#072at-read-only-asset-browser) и [отчёт](reports/07.2at-read-only-asset-browser.md).

### D179 — безопасная Asset-навигация из Game View диагностик (07.2as)

Renderer diagnostic entry может хранить типизированный AssetId, только когда ветвь обработки имеет конкретный зарегистрированный model/material. UI перед действием повторно разрешает ID через текущий AssetRegistry и требует совпадения project root с корнем контекста Project. Выбор лишь показывает тип, ID и registry-relative source path в Project; он не открывает, не переписывает и не переимпортирует источник. Неразрешённые/отсутствующие ресурсы остаются без asset-action, текст ошибок не парсится для поиска путей. Сохраняются Entity links 07.2ar и независимая отказоустойчивость остальных draw entries. Проверено Windows x64 BGFX D3D11: focused 6/6, full Release 93/93. Smoke управляет действием через harness, а не мышь; это не реализация полного Asset Browser. См. [архитектуру](ARCHITECTURE.md#выбор-зарегистрированного-ресурса-из-game-view-diagnostics-072as), [тесты](TESTING.md#072as-переход-к-зарегистрированному-ресурсу) и [отчёт](reports/07.2as-game-view-registered-asset-navigation.md).

### D178 — Entity links for Game View renderer diagnostics (07.2ar)

Сохранять структурированный current-frame snapshot renderer diagnostic entries с необязательным EntityId вместо попытки выводить объект из текста ошибки. Renderer добавляет ID только в ветвях, уже обрабатывающих конкретную Scene entity, а invalid active Camera3D связывается с camera EntityId; global errors (например, отсутствие камеры или Frame All) остаются без ссылки. Общий Diagnostics показывает сообщение и selection action, повторно использующий Editor selection, parent expansion и Hierarchy scroll. Перед показом действия EntityId повторно разрешается в открытой сцене. Ошибка одного меша не должна прерывать camera-driven submissions остальных объектов. Smoke проверяет вложенный unresolved model, точную выбранную сущность/родительскую цепочку и сохранение валидных draw calls. Не меняются Scene/Asset formats, публичный API, runtime и ресурсы. Не строить прямые файловые ссылки по непроверенному тексту importer/resolver. Ограничение проверки: Windows D3D11; button action вызывается harness-ом, не реальным мышиным вводом. См. [архитектуру](ARCHITECTURE.md#переход-из-game-view-diagnostics-к-сущности-meshmaterial-072ar), [тесты](TESTING.md#072ar-переход-от-game-view-diagnostics-к-meshrenderer) и [отчёт](reports/07.2ar-game-view-mesh-diagnostic-navigation.md).

### D177 — renderer diagnostics с camera link в Console (07.2aq)

Подключить текущий diagnostic snapshot Game View к существующему Editor Diagnostics окну, не добавляя его снова в ограниченный Recent diagnostics log каждый кадр. Если invalid-pose относится к выбранной active Camera3D, дать переход на её EntityId; при отсутствии active camera перечислять Camera3D-кандидатов для ручного выбора. Переход использует существующее выделение Entity и раскрывает только предков выбранной строки, затем прокручивает Hierarchy. Он не меняет `active`, сцены, сериализацию или команды. Ошибки mesh/assets остаются доступными как текст без ложной camera-ссылки. Smoke покрывает обе ветви. См. [архитектуру](ARCHITECTURE.md#3d-game-view-в-console-и-переход-к-camera3d-в-hierarchy-072aq), [тесты](TESTING.md#072aq-game-view-diagnostics-в-console-и-переход-к-camera3d) и [отчёт](reports/07.2aq-game-view-diagnostics-console-navigation.md).

### D176 — локализованная диагностика Game View и smoke-сценарии (07.2ap)

Отображать накопленную диагностику 3D target поверх Game View как ограниченный overlay, не менять схему сцены/команды и не останавливать 2D при отказе активной камеры. Известные сообщения локализовать; техническую причину оставить после переведённого префикса, неизвестные формулировки передавать общему fallback локализатора. CTest smoke запускает два отдельных временных Editor проекта: все камеры выключены и активная камера с нулевым Transform scale. Проверять, что UI действительно построил localized diagnostic, Game View не заявил camera-driven frame и не отправил 3D mesh, а исправная камера продолжает проходить прежний smoke. Не менять пользовательские сцены или ресурсы. Ограничение: текст рисуется поверх viewport и длинные сообщения могут быть обрезаны; проверены текущие Windows D3D11 и русский UI. См. [архитектуру](ARCHITECTURE.md#диагностики-3d-game-view-072ap), [тесты](TESTING.md#072ap-диагностики-активной-camera3d-в-game-view), [отчёт](reports/07.2ap-game-view-diagnostics.md).

### D175 — активная Camera3D в Game View (07.2ao)

В Editor Game View разрешить 3D MeshRenderer pass только через явно выбранную активную Scene Camera3D. Композировать pose камеры по той же parent-to-child матричной convention, что и иерархические меши; преобразовывать local -Z forward и +Y up и использовать FOV/clip planes компонента. Aspect вычислять из фактического pixel-size framebuffer, держать depth target. Сохранить Scene View orbit camera и текущий 2D orthographic pass; Game View 2D остаётся поверх 3D как явно ограниченный overlay. При отсутствии/ошибке камеры пропустить 3D и продолжить 2D с диагностикой. Не менять Scene schema или командный API; runtime/Play camera, Game View материалы/PBR, свет/тени и согласованная 2D/3D глубина отложены. См. [архитектуру](ARCHITECTURE.md#активная-scene-camera3d-в-game-view-072ao), [проверки](TESTING.md#072ao-активная-camera3d-в-game-view), [3D guide](3D_RENDERING.md#active-camera-in-editor-game-view-072ao) и [отчёт](reports/07.2ao-active-camera-game-view.md).

### D174 — активная Camera3D и Scene v9 (07.2an)

Хранить выбор камеры в Scene как `Camera3DComponent::active` schema v2 и валидировать максимум одну активную камеру. Добавление, удаление, lens update и смена выбора проходят через typed transactional command API; Undo/Redo возвращают исходный компонент с тем же ComponentId и восстанавливают прежний active EntityId. Inspector применяет FOV и clip planes явно, активность — сразу. Scene v9 добавляет 0/1 active после v1 lens-полей; Scene v8 Camera3D v1 загружается неактивной, без автоматической записи файла. Active selection пока не подключена к Game View/runtime. См. [FILE_FORMATS](FILE_FORMATS.md#astro-scene-v9), [TESTING](TESTING.md#072an-активная-camera3d-и-inspector), [отчёт](reports/07.2an-active-camera-inspector.md).


### D172 — Scene Camera3D как опциональные данные авторинга (07.2am)

Добавить к Entity `Camera3DComponent` с вертикальным FOV и near/far planes, оставляя pose в существующем Entity Transform. Валидировать конечность и диапазоны `0 < FOV < 180`, `0 < near < far`. Сохранять компонент с stable ComponentId в Scene v8; v1–v7 читать без добавления камеры и обновлять версию только при явном сохранении. Эта модель данных сама по себе не выбирает активную камеру, не преобразует Transform в view matrix и не рендерит Game View. См. [FILE_FORMATS](FILE_FORMATS.md#astro-scene-v8), [TESTING](TESTING.md#072am-scene-camera3d-и-scene-v8) и [отчёт](reports/07.2am-scene-camera-component.md).

### D173 — локализованный MSVC /showIncludes prefix при Ninja (07.2am)

Если MSVC печатает `/showIncludes` с локализованным prefix, разрешить проектную cache option `ASTRO_MSVC_SHOWINCLUDES_PREFIX` и передать её в `CMAKE_CL_SHOWINCLUDES_PREFIX` до генерации target rules. Без корректного prefix CMake/Ninja не сохранял реальные header dependencies; инкрементальная сборка оставляла старые объекты после изменения публичного Scene header, что привело к ABI mismatch. На русской локали с prefix `Примечание: включение файла:` clean Release build записал header dependencies и полный CTest прошёл. При изменении prefix требуется одноразовая clean rebuild. См. [BUILD_GUIDE](BUILD_GUIDE.md#русская-локаль-msvc-с-ninja) и [отчёт](reports/07.2am-scene-camera-component.md).

## D169 — адаптивная layout-политика Editor (07.2aj)

Размер нового Editor окна ограничивается `SDL_GetDisplayUsableBounds` и окно центрируется на основном дисплее; на display/scale/resize events границы пересчитываются по текущему экрану. Dock layout выбирает compact split при ширине окна меньше 1000 SDL logical pixels, wide split в остальных случаях. Game View получает bounded first-use size и может быть уменьшен после перехода к более тесной геометрии; пользовательская раскладка не перестраивается автоматически. Политика вынесена в чистую функцию и покрыта unit-тестом. Развилки DPI и multi-monitor остаются платформенным SDL поведением, для которого нужен автоматический SDL window integration test. Full CTest при Windows 125% пока имеет 2 compact alpha screenshot failures; диагноз не установлен. Системный scale после ручного прогона не удалось подтвердить возвращённым: Settings показывал пустое поле и ссылку Custom scaling, которая могла бы затронуть все дисплеи; глобальный scale не меняли. См. [отчёт 07.2aj](reports/07.2aj-responsive-editor-layout.md).

## D168 — масштабировать Editor UI от SDL display scale (07.2ai)

Editor использует `SDL_GetWindowDisplayScale(window)` как источник UI scale, отдельно от pixel density/framebuffer scale. Перед новым ImGui кадром `EditorUiScalePolicy` сравнивает scale с последним применённым; при изменении восстанавливает неизменную копию theme style, вызывает `ScaleAllSizes(scale)` и задаёт `FontScaleMain=scale`. Это позволяет менять DPI без накопительного scaling и без перезапуска. Не использовать экспериментальный ImGui auto-font scaling как единственную политику: он не масштабирует весь style. Invalid scale fallback=1; поддерживаемый диапазон текущего Windows Editor — 1..4, поэтому display scale ниже 100% не уменьшает UI. Проверены реальные Windows 100/125 и live transition; адаптивное перераспределение узких панелей остаётся отдельной задачей. См. [отчёт 07.2ai](reports/07.2ai-editor-dpi-scaling.md).

## D164 — не имитировать реальный Windows DPI программным флагом (07.2af)


Для Windows high-DPI диагностики фиксировать оба SDL значения: `SDL_GetWindowDisplayScale` (scale интерфейса/content, соответствующий настройке дисплея) и `SDL_GetWindowPixelDensity` (ratio backbuffer pixels/window size). Подтверждать системный scale также через DPI-aware HWND. На 125% smoke окно было PerMonitorV2, DPI120, `displayScale=1.25`, `pixelDensity=1.0`. Поэтому density1 не означает, что Windows scaling не работает; оно означает отсутствие отдельного pixel-density factor. Не менять реестр или compatibility override ради теста; настройку дисплея можно менять только по явному разрешению и с восстановлением. См. D165, D166 и [отчёт](reports/07.2ag-sdl-windows-dpi-metrics.md).

## D165 — временное изменение масштаба только для проверки (07.2af)

Изменять масштаб Windows Display Settings для ручного high-DPI smoke только после явного разрешения пользователя, до изменения записывать исходное значение каждого затрагиваемого дисплея и после прогона восстанавливать его. 07.2af/07.2ag установили 125% на двух активных дисплеях, проверили 6 ramp smoke и вернули исходные 100%. Реальный HWND подтвердил DPI120; SDL `displayScale=1.25` и `pixelDensity=1.0` — разные, согласованные метрики. Системные настройки не являются частью автоматического CTest.

## D167 — не смешивать DPI-масштабирование ImGui и framebuffer-координаты (07.2ah)

Пусть SDL3 platform backend сообщает ImGui scale монитора через `SDL_GetDisplayContentScale`, но UI policy должен явно применять этот scale. В текущем Editor `io.ConfigDpiScaleFonts` и `io.ConfigDpiScaleViewports` выключены; основной шрифт загружается фиксированным размером 16 px, а размеры style отдельно не адаптированы. Даже включение `ConfigDpiScaleFonts` масштабирует шрифт, не spacing/padding. Для целостной поддержки нужен DPI policy, который пересчитывает размеры style от сохранённой базовой копии без накопительного повторного scale и проверяет смену монитора; пользовательские настройки Display не менять ради автоматических тестов. Не подменять им `io.DisplayFramebufferScale`, отвечающий за отображение координат draw data и backbuffer. Реализация отложена до отдельного подэтапа. См. [оценку 07.2ah](reports/07.2ah-editor-imgui-dpi-scaling-assessment.md).

## D166 — разделять SDL display scale и pixel density (07.2ag)

Логировать и интерпретировать `SDL_GetWindowDisplayScale` отдельно от `SDL_GetWindowPixelDensity`. Первый отражает display/content scale; второй вычисляется как отношение размера окна в пикселях к размеру окна в SDL координатах. При Windows PerMonitorV2 125% оба значения могут быть 1.25 и 1.0 соответственно. Проверять DPI через HWND отдельно; не вводить ошибку только из-за `pixelDensity=1`. Smoke выводит оба значения для дальнейшей диагностики.

## D163 — пространственные MASK smoke при разных размерах окна (07.2ae)

Проверять alpha-ramp Scene View при логических размерах окна 900×650, 1100×720 и 1440×900. Классифицировать каждый из 64 вертикальных bins по доле mask/blend пикселей относительно числа sample pixels этого bin; сохранять требования нескольких областей обоих типов и трёх переходов, не требуя конкретного начального типа. Большое окно запрашивает `SDL_WINDOW_HIGH_PIXEL_DENSITY`, а лог сообщает логический размер, размер окна в пикселях и `SDL_GetWindowPixelDensity`. Этот флаг не меняет настройки Windows DPI и не гарантирует плотность >1; текущий дисплей сообщил 1.0 во всех профилях, поэтому подтверждены разные размеры, но не разные реальные DPI. Это тестовый контракт smoke, не визуальный golden и не кросс-GPU гарантия. См. [отчёт](reports/07.2ae-window-density-spatial-smoke.md).

### D162 — пространственные проверки MASK alpha-рампы (07.2ad)

Существующие общие pixel totals доказывают только одновременное наличие mask и blend в Scene View crop, но не их пространственную связь с U-рампой. Для двух Windows D3D11 cutoff-transition smoke увеличить тестовую поверхность только в smoke режиме (zoom wheel +5), разделить crop на 64 равные вертикальные колонки и считать magenta/cyan пиксели по существующим широким color classifiers. Колонка mask-rich: >=120 mask и mask >= 2×blend; blend-rich: >=100 blend и blend >=2×mask. Требовать сначала blend-rich (низкий U alpha < cutoff), затем минимум две rich-колонки каждого типа и минимум три смены доминирующего класса; neutral/mixed bins разрешены. Оба масштаба показывают B→M→B→M. Это пространственное count-based smoke, не проверка точной координаты каждого texel или golden image. Числа зависят от окна/растеризации и текущего D3D11 адаптера; не утверждать межплатформенную эквивалентность. Публичные API, runtime, схема и renderer policy не меняются. См. [TESTING](TESTING.md#072ad-пространственная-проверка-mask-alpha-рампы), [ROADMAP](ROADMAP.md#072ad-пространственная-проверка-mask-alpha-рампы-completed-prototype), [3D rendering](3D_RENDERING.md#пространственная-проверка-mask-alpha-рампы-072ad) и [отчёт](reports/07.2ad-mask-spatial-transition.md).

### D161 — локальная alpha-рампа вокруг MASK cutoff (07.2ac)

Дополнить широкую рампу и дискретные boundary/band тесты двумя Editor Scene View smoke с 64×64 UNORM8 alpha, плавно растущей от 112 до 143 вокруг cutoff 0,5. Применять MASK, Repeat, `LinearMipmapLinear` и UV-scale 2/4; проверять mipmapped MASK submission, видимую маску и blend-through. Обе текущие Windows D3D11 проверки прошли с 351/297 и 378/249 MASK/blend пикселями. Нет изменений runtime, материала, схемы, API или зависимостей. Диапазоны широкие и не соответствуют конкретным координатам/LOD; не заявлять golden image или переносимость. См. [TESTING](TESTING.md#072ac-локальная-alpha-рампа-вокруг-mask-cutoff), [3D rendering](3D_RENDERING.md#локальная-alpha-рампа-вокруг-mask-cutoff-072ac), [ROADMAP](ROADMAP.md#072ac-локальная-alpha-рампа-вокруг-mask-cutoff-completed-prototype) и [отчёт](reports/07.2ac-mask-cutoff-transition-smoke.md).

### D160 — смешанные alpha-полосы MASK mip (07.2ab)

Расширить cutoff-edge проверку с отдельных однотонных текстур на одну mip-фильтруемую текстуру, содержащую вертикальные полосы alpha 127, 128, 129 и 127. При cutoff 0,5 ожидаются одновременно отрисованные MASK-участки и discard полос 127 с видимым blend-through; запускать на UV-scale 2 и 4. Реализовано двумя Editor D3D11 smoke-кейсами на временной текстуре внутри disposable-проекта; они измерили соответственно 358/303 и 378/252 MASK/blend пикселей и по одной mipmapped MASK подаче. Это подтверждает только грубое сосуществование результатов порога на текущем Scene View пути; не проверяет конкретные координаты пикселей, выбранный аппаратный LOD или другие GPU/backend. Не менять runtime и пользовательские ассеты. Без изменения API, схемы, зависимости или renderer policy. См. [TESTING](TESTING.md#072ab-смешанные-alpha-полосы-mask-cutoff), [документацию 3D](3D_RENDERING.md#смешанные-alpha-полосы-mask-cutoff-072ab), [дорожную карту](ROADMAP.md#072ab-смешанные-alpha-полосы-около-mask-cutoff-completed-prototype) и [отчёт](reports/07.2ab-mixed-mask-boundary-smoke.md).

### D159 — MASK cutoff-edge UNORM8 mip smoke (07.2aa)

Add a mipmapped Scene View smoke using constant per-image alpha bytes below and above the shader cutoff boundary: 127/255, 128/255 and 129/255 against cutoff 0.5. Run each at UV scales 2 and 4 with the existing Repeat plus `LinearMipmapLinear` MASK material path. Assert 127/255 is discarded and exposes blend-through; assert the first representable passing value 128/255 and 129/255 remain visible. Require a mipmapped MASK submission and print the tested byte/cutoff. Keep source images confined to the disposable test project and retain existing binary/fractional alpha tests. Because UNORM8 has no value exactly 0.5, do not label 128/255 as exact equality. No runtime, schema, API or dependency change. All six current Windows D3D11 cases pass; this does not establish GPU-driver or backend parity. See [TESTING](TESTING.md#072aa-mask-cutoff-edge-unorm8-mip-smoke), [3D rendering](3D_RENDERING.md#mask-cutoff-edge-unorm8-mip-smoke-072aa), and [report](reports/07.2aa-mask-cutoff-edge-smoke.md).

### D158 — Partial-alpha MASK cutoff minification sweep (07.2z)

Extend the temporary Windows Editor Scene View alpha-ramp smoke across authored MASK cutoffs 0.25 and 0.75 at both existing UV scales 2 and 4. Keep cutoff 0.5 near/far smokes as regression. Each case uses the same generated 64×64 horizontal 8-bit alpha ramp, Repeat sampler, `LinearMipmapLinear` filtering, and the existing broad screenshot range plus blend-through assertion. Generated texture remains confined to the disposable smoke project. All four cases pass with a mipmapped MASK texture submission. Near/far magenta/blend counts: cutoff 0.25 = 486/174 and 486/123; cutoff 0.75 = 182/396 and 162/421. No API, schema, renderer or dependency change. This is a coarse current Windows D3D11 check, not exact LOD readback, a pixel golden, arbitrary-pattern validation or cross-driver parity. Full test results are in [TESTING](TESTING.md#072z-partial-alpha-mask-cutoff-minification-sweep) and the [report](reports/07.2z-partial-alpha-mask-cutoff-smoke.md).

### D157 — Partial-alpha MASK minification smoke (07.2y)

Extend the existing Editor Scene View MASK mip screenshot smoke with one generated 64×64 magenta RGBA texture whose alpha ramps horizontally from fully transparent to fully opaque. Run the disposable project at the existing near/far UV scales 2 and 4 with MASK cutoff 0.5, Repeat and `LinearMipmapLinear`. Require the current mask mipmapped texture submission, a bounded visible mask pixel range and visible blend-through under discarded fragments. Keep existing binary split/checker/hole and cutoff tests. Write generated image bytes only under the temporary smoke project; do not touch user assets. No material format, public API, dependency or renderer policy change. Both Windows 11 x64 D3D11 cases passed (near 351 mask / 297 blend; far 331 / 286). Pixel counts are broad current-driver assertions, not a golden image, exact LOD readback or cross-driver promise. Full regression results are in [TESTING](TESTING.md#072y-partial-alpha-mask-minification-smoke) and the [report](reports/07.2y-partial-alpha-mask-smoke.md).

### D156 — Fractional-alpha MASK coverage sweep (07.2x)

Assess the existing nearest-achievable MASK mip coverage selector with partially transparent input, in addition to binary alpha shapes. Add deterministic alpha-step and horizontal alpha-ramp fixtures at 64×64 and odd 31×17 dimensions and evaluate authored cutoffs 0.25, 0.50 and 0.75. For each mip, compare measured coverage to an independent enumeration of outcomes reachable by alpha scaling and UNORM8 quantization; retain the source-byte immutability check. All tested mips select the nearest achievable coverage, so make no runtime or public-contract change. Tiny levels remain constrained by discrete coverage fractions. No dependency, schema, API or GPU change. Windows x64 Release focused unit passed; full CTest result is recorded in [TESTING](TESTING.md#072x-fractional-mask-alpha-and-cutoffs). See [3D rendering](3D_RENDERING.md#fractional-alpha-mask-coverage-sweep-072x) and [report](reports/07.2x-fractional-mask-alpha.md).

### D155 — MASK mip coverage candidate selection (07.2w)

Measure coverage at the authored mip-0 cutoff for split, checker and center-hole alpha patterns using 64×64 and odd 31×17 source images. Keep the existing color filter, alpha cutoff semantics, texture format and source asset policy. The earlier 24-iteration binary search could stop across an 8-bit quantization boundary: in the 31×17 checker case it produced 4/6 coverage on a 3×2 mip although 3/6 was attainable. Evaluate deterministic alpha-bin threshold candidates instead, score and write candidates through the same UNORM8 conversion, and select the candidate with the smallest absolute coverage error. Tests independently enumerate achievable outcomes and check each mip; source bytes remain unchanged. This improves selection among representable outcomes, not representability itself: tiny mip levels can still differ substantially from the original coverage. No API, schema or dependency change. Windows x64 Release focused unit and full CTest passed 66/66. See [TESTING](TESTING.md#072w-mask-mip-coverage-accuracy), [3D rendering](3D_RENDERING.md#mask-mip-coverage-accuracy-072w), and [report](reports/07.2w-mask-mip-coverage-precision.md).

### D154 — Additional MASK alpha mip patterns (07.2v)

Broaden the temporary Editor MASK mip fixture from a vertical split to include a block checker and centered transparent hole, keeping the split near/far cases as regression. Use 16×16 checker blocks for 64×64 source pixels and test checker at UV scale 2; the initially tried 4×4 blocks at scale 4 averaged to a uniform 50% alpha mip with unrepresentable partial coverage and correctly vanished. The checker was adjusted to retain opaque/transparent mip texels at the selected test range. Hole has 75% opaque base coverage and runs at scale 4. Preserve the same D3D11 pixel crop policy: count diagnostic magenta mask pixels and underlying blend pixels, require partial coverage, and confirm MASK mip upload. All PNGs live only in disposable smoke projects. This tests chosen patterns/scales, not arbitrary assets or other GPUs; it does not change the coverage-rescaling algorithm. See [TESTING](TESTING.md#072v-mask-alpha-pattern-smoke) and the [report](reports/07.2v-mask-mip-pattern-smoke.md).

### D153 — Nonuniform MASK alpha mip smoke (07.2u)

Exercise the 07.2s MASK mip policy with an isolated Editor project image rather than a factor-only alpha. Generate a 64×64 magenta texture whose left half is opaque and right half transparent, register the temporary image in the smoke project, and bind it to the MASK preview with cutoff 0.5, Repeat wrapping and `LinearMipmapLinear`. Run two U transform scales (2 and 4) to change minification/selected LOD while preserving the same source alpha coverage. Require a MASK mesh with uploaded mip levels, a broad nonzero/partial magenta pixel count, and visible translucent pixels beneath the discarded texels. Do not edit repository/user image assets or change shader/mip semantics. The assertion is specific to Windows D3D11 and this deterministic fixture; exact LOD selection and cross-driver coverage remain unasserted. No runtime format/API/dependency change. See [TESTING](TESTING.md#072u-mask-alpha-mip-smoke) and the [report](reports/07.2u-mask-mip-coverage-smoke.md).

### D152 — Rendered MASK cutoff boundary validation (07.2t)

Keep shader MASK semantics `discard when sampled alpha < cutoff`, so equality survives. Validate on Windows D3D11 with three isolated Editor smoke runs using alpha 0.49, 0.50 and 0.51 against cutoff 0.50. Give the test material a magenta diagnostic base color and inspect only the normalized Scene View screenshot crop: below requires no classified magenta pixels, while equal/above each require a minimum visible count. Preserve the existing below-cutoff-under-BLEND overlap assertion. The classifier accommodates observed D3D11 sRGB output near (149,19,149); it does not alter renderer/shader behavior and is not a general color golden. No asset schema, runtime API or dependency changes. Cross-driver/OS tests and arbitrary texture-alpha boundary images remain outside this test. See [TESTING](TESTING.md#072t-mask-cutoff-pixel-smoke) and the [07.2t report](reports/07.2t-mask-cutoff-pixel-smoke.md).

### D151 — Alpha-aware base-color mips (07.2s)

Enable base-color mipmaps for Scene View MASK/BLEND materials using mode-specific CPU preparation. OPAQUE retains its existing linear-space area average. BLEND averages premultiplied linear RGB and alpha, then unpremultiplies RGB for the existing straight-alpha shader/blend contract; fully transparent texel RGB therefore does not tint visible edge texels. MASK uses the same color filter and independently rescales each mip alpha toward the base-level coverage fraction measured at the authored `alphaCutoff`. Quantized 8-bit alpha and small dimensions can make the exact fraction impossible; select the nearest representable result with a bounded 24-step search over a 256-bin histogram. Include alpha mode and cutoff in the prepared GPU texture cache key. Validate cutoff semantics as alpha `< cutoff` discard (equality survives), color fringe and coverage with CPU unit tests, and retain the existing Windows D3D11 below-cutoff overlap smoke. Do not modify source images/materials or material schema; no dependency added. This does not provide cross-driver visual goldens, Game View support or other backend validation. See [07.2s TESTING](TESTING.md#072s-alpha-aware-base-color-mips), [3D rendering](3D_RENDERING.md#alpha-aware-base-color-mips-072s), and [report](reports/07.2s-alpha-aware-base-color-mips.md).

## D001 — Предварительный технологический стек

- Дата: 2026-11-12.
- Статус: предложение; окончательное принятие отложено до прототипа 00.3.
- Основание: раздел 03 требований пользователя.
- Предложение и критерии проверки: [TECH_STACK.md](TECH_STACK.md).
- Ограничения: не фиксировать неподтверждённые версии; кандидаты не становятся обязательными зависимостями автоматически.
- Требование к архитектуре: адаптеры рендеринга, физики, звука и импорта должны позволять заменять библиотеки без переписывания ядра.
- Проверка: совместимость, лицензии и фактическая сборка ещё не подтверждены. Установки и загрузки зависимостей не выполнялись.

## D002 — Слои архитектуры и контракты

- Дата: 2026-11-12.
- Статус: архитектурные требования пользователя; реализация отсутствует.
- Основание: раздел 04 и принципы P01–P14.
- Решение: единый Typed Command API перед Document/Scene Model и Engine Core; ядро без окна, необязательные runtime-модули, отдельные Editor и Export.
- Контракты Core, команд, сцен, ресурсов, префабов, сериализации и runtime описаны в [ARCHITECTURE.md](ARCHITECTURE.md).
- Ограничения: AI не обходит команды; необратимые runtime-действия документируются; исходники ресурсов сохраняются; Play Mode изолирован от редактируемой сцены.
- Следствия: конкретные API и схемы требуют дальнейшего проектирования и проверки; перечисление компонента или команды не означает их готовность. Предварительный стек D001 не становится окончательным.

## D003 — Графический редактор и UX

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 05, P01, P04, P05 и P10.
- Требования к областям редактора, рабочим пространствам, операциям, материалам, камерам и UI записаны в [EDITOR_UX.md](EDITOR_UX.md).
- Решение: редактирование свойства через GUI эквивалентно Command API; необходимы Undo/Redo, история и обработка конфликтов и несохранённых правок.
- Ограничения: узловой редактор шейдеров и кат-сцены предусмотрены позже; макеты и очередность реализации не утверждены.

## D004 — Выборочное сохранение и Checkpoint Editor

- Дата: 2026-11-12.
- Статус: обязательные требования пользователя; реализация отсутствует.
- Основание: раздел 06.
- Решение: разработчик явно выбирает сохраняемые объекты, компоненты и свойства через профили/правила. Автоматическое сохранение всего мира без этого выбора запрещено.
- Save System & Checkpoint Editor обязателен как модуль продукта. Правила включения в экспортируемую игру требуют дальнейшего определения с учётом P02.
- Режимы «только точка возрождения» и «запись полного выбранного состояния» разделены; respawn настраивается отдельно. Временные эффекты по умолчанию не сохраняются.
- Настройки хранятся в проекте, прогресс игрока — отдельно в пользовательских каталогах платформы. Требуются атомарная запись, резервная копия, версии, миграции и диагностика повреждений; защита от читов не обещается.
- Полный перечень требований и будущих проверок: [SAVE_SYSTEM.md](SAVE_SYSTEM.md).

## D005 — Игровые системы после проверенной основы

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 07.
- Решение: игровые системы и расширения из [GAME_SYSTEMS.md](GAME_SYSTEMS.md) добавляются только после проверенной основы и по явно назначенным подэтапам.
- Ограничения: отдельный core для машин не создаётся; сохранение состояния NPC выполняется по выбору; возможности воды зависят от бюджета платформы.
- Следствия: критерии готовности основы, порядок реализации и численные бюджеты ещё предстоит определить. Новые зависимости этим решением не утверждены.

## D006 — Работающие модульные шаблоны игр

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 08.
- Решение: каждый шаблон T01–T10 создаёт минимальную работающую игру, изменяемую через редактор и Codex, с инструкцией и автоматическими smoke-тестами.
- Каталог и условия приёмки: [GAME_TEMPLATES.md](GAME_TEMPLATES.md).
- Ограничения: шаблоны не создаются до реализации необходимых подсистем; оружие можно отключить с сохранением FPS-контроллера; камеры и управление переиспользуются между жанрами.
- Следствия: порядок реализации, структура пакетов и конкретные smoke-сценарии определяются в будущих назначенных подэтапах.

## D007 — Необязательная AI-интеграция с ограниченными полномочиями

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 09.
- Решение: локальный AI Editor API через типизированные команды и отдельный адаптер Codex; игра и редактор запускаются без интеграции.
- Пайплайн: запрос → план → проверка схемы/разрешений → dry-run/diff → подтверждение рискованных операций → транзакция → тесты → отчёт.
- Ограничения: разрешённые директории, sandbox сборок/тестов, ограниченный доступ к секретам и сети, аудит, контроль затрат/времени и остановка долгих операций.
- AI Game Tester использует детерминированные сценарии и собирает свидетельства регрессий; отсутствие ошибок не гарантируется. Оптимизации проверяются на поведение и визуальные отличия.
- Полные требования: [AI_INTEGRATION.md](AI_INTEGRATION.md). Механизмы безопасности и конкретные технологии пока не выбраны и не проверены.

## D008 — Дорожная карта и критерии приёмки

- Дата: 2026-11-12.
- Статус: план пользователя; подэтапы реализации не начаты.
- Основание: раздел 10 из приложенного текста.
- Решение: этапы 00–10, подэтапы и критерии приёмки сохранены в [ROADMAP.md](ROADMAP.md). Каждый подэтап запускается по явному указанию и завершается отчётом и остановкой.
- Уточнение D005/D006: порядок работ теперь задан дорожной картой; конкретные тестовые сценарии, численные бюджеты и детали реализации уточняются в соответствующих подэтапах.
- Ограничения: стек не утверждается до 00.3; портирование подтверждается реальными целевыми toolchains, Windows сохраняет работоспособность; iOS-сборка на Windows без совместимого Mac не обещается.
- Наличие документации не подтверждает приёмку 00.1: необходимы минимальный app и тест, чистая конфигурация CMake, сборка, CTest и инструкции Windows.

## D009 — Проверки каждого этапа и готовность функции

- Дата: 2026-11-12.
- Статус: требования пользователя; исполняемая тестовая инфраструктура отсутствует.
- Решение: применять unit/integration/smoke/regression-проверки и специальные проверки графики, производительности, сохранений и безопасности согласно [TESTING.md](TESTING.md).
- Каждая готовая функция имеет документацию пользователя, пример, тест и известные ограничения. Целевой комплект документов перечислен в TESTING.md; отсутствие документов и тестов отмечается явно.
- Ограничения: бюджеты производительности утверждаются отдельно; визуальные допуски пока не определены. Невыполнимые проверки требуют объяснения и не считаются пройденными.

## D010 — Предварительная структура репозитория

- Дата: 2026-11-12.
- Статус: предложение пользователя; структура реализации ещё не создана.
- Основание: раздел 12.
- Предложение сохранено в [REPOSITORY_STRUCTURE.md](REPOSITORY_STRUCTURE.md); изменения допускаются с обоснованием.
- Согласование требований: docs/TEMPLATE_GUIDE.md остаётся обязательным согласно D009, несмотря на отсутствие в списке раздела 12. Существующие документы и .gitignore сохраняются.
- Ограничения: LICENSE только после выбора лицензии; third_party только по согласованной политике зависимостей. Ни лицензия проекта, ни политика пока не выбраны.
- Следующий подэтап реализации структуры — 00.1, по явному указанию пользователя.

## D011 — Минимальная основа сборки 00.1

- Дата: 2026-11-12. Основание: явное назначение 00.1 пользователем.
- Созданы отдельная цель astro_headless в engine/runtime, корневой CMake и CTest smoke-тест headless.startup без внешних библиотек. C++20 обязателен, расширения языка отключены.
- Выбран минимальный CMake 3.20 и presets schema 2; это требование конфигурации, а не подтверждение проверенной версии toolchain. Windows preset использует Ninja и уже настроенное окружение компилятора.
- Создаются только каталоги с содержательными файлами; остальные из предложения D010 добавляются по мере реализации. Пустые модули и фиктивный core не создаются.
- Используется smoke-тест процесса: ядра для unit-тестов ещё нет. Дальнейшие группы тестов остаются обязательными по D009.
- Существующее ТЗ сохранено. Приёмка сборки отложена: CMake/CTest и компилятор не обнаружены; установки не выполнялись. 00.2 не начат.

## D012 — Локальные проверки и диагностика 00.2

- Дата: 2026-11-12. Основание: явное назначение 00.2 пользователем при незавершённой инструментальной приёмке 00.1.
- Выбран локальный PowerShell 7+ pipeline: новый каталог сборки каждого запуска, JSON-отчёт, версии инструментов, SHA-256 входных файлов, отдельные stdout/stderr, тайм-ауты и ненулевые коды при ошибках/неполной проверке. CI-сервис не настраивался.
- clang-format проверяет исходники без перезаписи; clang-tidy включается при наличии через CMake option. Инструменты не устанавливаются; неподтверждённые версии не фиксируются.
- Логирование ограничено уровнями сообщений минимального CLI. stdout получил префикс [info]; для потребителей точного текста нужна адаптация. Командный API движка отсутствует и не изменён. --help возвращает 0, неверные аргументы — 2, сбой вывода/исключение — 1. Logger/Result этапа 01 не реализуются.
- 5 проверок запуска процессов и 9 проверок реального отчёта прошли. Полная приёмка C++ и форматирования остаётся заблокированной отсутствием инструментов; 00.3 не начат.

## D013 — Результат технического spike SDL3/bgfx/ImGui

- Дата: 2026-11-13. Основание: явное назначение 00.3 и отдельное разрешение пользователя на установку инструментов/загрузку библиотек.
- Windows x64/MSVC/SDK сборка работает с SDL 3.5.0 исходников, bgfx, bx, bimg и Dear ImGui docking `1.93.1 WIP`; ревизии приведены в [reports/00.3.md](reports/00.3.md).
- SDL3 создаёт окно и передаёт ввод; bgfx D3D11 обслуживает swap chain и рисует ImGui draw lists; ImGui docking строит панели. Отдельный renderer backend bgfx необходим, поскольку upstream SDL3 backend ImGui сам по себе не реализует bgfx renderer.
- Сборка и CTest spike прошли (4/4); автоматический сценарий подтвердил кадры, docking, клавиатуру, resize, draw calls. Решение подтверждено только для этого набора ревизий и окружения.
- Статус предварительного стека остаётся предварительным. Не подтверждены другие ОС/бэкенды, release package, размер release, полный транзитивный аудит лицензий и ручное UX-тестирование drag-and-drop. ImGui выбранной ревизии помечен WIP.
- Локальные зависимости остаются в игнорируемом `build/spike-deps`; `ASTRO_BUILD_STACK_SPIKE` выключен по умолчанию. Следующий этап 01 не начат.

## D014 — Базовые сервисы ядра 01.1

- Дата: 2026-11-13. Основание: пользователь начал этап 01; выполнен только первый подэтап 01.1.
- Core выделен в отдельную библиотеку `astro_core`, независимую от SDL, bgfx и GUI. `EngineContext` явно владеет Logger и ConfigurationService.
- `Result<T>` и `Result<void>` используют ErrorCode + сообщение; ошибки возвращаются как значения.
- Logger использует sink injection и порог severity, с простым stderr/clog sink по умолчанию.
- Для конфигурации выбран минимальный UTF-8 key=value формат без внешней зависимости. При загрузке состояние заменяется только после полного успешного разбора; это не утверждение окончательного формата сериализации.
- На момент завершения 01.1 объём не включал ModuleRegistry, lifecycle, EventBus, TimeService, команды, JSON, редактор или игровые системы; ModuleRegistry принят отдельно решением D015.

## D015 — Контракт ModuleRegistry 01.2

- Дата: 2026-11-13. Основание: пользователь продолжил следующим подэтапом после 01.1; выполнен только 01.2.
- `IEngineModule` предоставляет имя, статический список имён зависимостей и `initialize`/`shutdown` через общий `EngineContext`; `ModuleRegistry` владеет модулями через `unique_ptr`.
- Зависимости разрешаются детерминированным DFS/topological traversal в порядке регистрации и указанном порядке зависимостей. Неизвестные, пустые/дублирующиеся, самозависимые зависимости и циклы отклоняются до начала инициализации.
- Ошибка/исключение initialize возвращается через `Result`; уже успешно запущенные модули получают shutdown в обратном порядке. Модуль с неуспешным initialize обязан освободить собственные частичные ресурсы самостоятельно.
- Успешный lifecycle одноразовый: после него реестр нельзя менять или запускать повторно. Ошибка shutdown агрегируется, но не прерывает завершение остальных модулей.
- `tick` и его политика расписания не входят в 01.2; они остаются для 01.3. Тесты покрывают основные ошибки, порядок и rollback. Потокобезопасность и динамическая регистрация во время исполнения не предоставляются.

## D016 — EventBus и fixed timestep 01.3

- Дата: 2026-11-13. Основание: пользователь назначил следующий подэтап после 01.2; выполнен только 01.3.
- EventBus — header-only typed API на C++ type identity с монотонными subscription IDs. Dispatch синхронный и не потокобезопасный. Snapshot даёт определённое поведение при subscribe/unsubscribe внутри callback; исключения callback агрегируются как `InternalError` после продолжения рассылки.
- TimeService получает реальный frame delta от вызывающего кода, принимает только конечные неотрицательные значения, ограничивает frame delta, выдаёт число fixed steps до настраиваемого catch-up cap, остаток для интерполяции и величину отброшенного времени. Настройки по умолчанию: 1/60 s, clamp 0.25 s, максимум 8 шагов на advance.
- Pause не продвигает simulation time; при переходе в паузу дробный остаток accumulator очищается. Интерполяция/рендеринг не реализованы.
- `ModuleRegistry::tick(fixedDelta)` последовательно обновляет запущенные модули; вызывающая сторона использует `FrameTiming.fixedStepCount` для количества вызовов. Ошибка tick возвращается вызывающей стороне и не запускает автоматический shutdown.
- Тесты покрывают публикацию/ошибки callbacks, мутацию подписок во время dispatch, clamping, catch-up, dropped time, паузу, валидацию и порядок module tick. Потокобезопасность EventBus и измерение wall clock в ядро не входят.

## D017 — Ранний typed CommandRegistry 01.4

- Дата: 2026-11-13. Основание: пользователь назначил следующий подэтап после 01.3; выполнен только 01.4.
- Схема команды содержит dotted ID и положительную версию; при регистрации фиксируются точные C++ типы payload/output, обязательные validator и executor. ID уникален в пределах registry; одновременно хранится одна версия команды.
- `validate` является validation-only/dry-run и не вызывает executor; `execute` повторно валидирует payload и возвращает `Result<Output>`. Ошибки неизвестного ID, версии и типов диагностируются отдельными ErrorCode. Исключения callbacks преобразуются в InternalError.
- Локальный audit сохраняет последовательность операций validate/execute и ошибок без payload. Он не является persistence или защищённым журналом.
- Межпроцессные/JSON schema, dry-run diff, AI permissions, операции над сценами, rollback/транзакции и Undo/Redo не входят в 01.4. Executor, который меняет состояние, а затем падает, автоматически не откатывается; до 01.5 такие команды ограничены.
- Registry не потокобезопасен; output должен быть копируемым или `void`; миграции версий и совместная регистрация версий пока не поддерживаются.

## D018 — Транзакции и история команд 01.5

- Дата: 2026-11-13. Основание: пользователь продолжил со следующего подэтапа после 01.4; выполнен только 01.5.
- `CommandTransaction` содержит последовательность именованных apply/undo callbacks. `TransactionHistory::commit` применяет шаги по порядку; при ошибке компенсирует успешный префикс в обратном порядке. Пустая транзакция — допустимый no-op без записи в историю.
- Undo идёт в обратном порядке, Redo — в прямом. Новая успешная транзакция после Undo удаляет redo-ветку.
- Ошибка одного undo/redo вызывает компенсацию уже выполненного подмножества; если компенсация не удалась, история очищается. Ошибка rollback при commit также очищает историю. Все ошибки восстановления возвращаются с диагностикой.
- Каждый отдельный callback обязан быть атомарным или самокомпенсирующимся: менеджер может откатить только шаги, завершившиеся успехом.
- `CommandRegistry::registerTransactionalCommand` строит шаги, которые registry применяет и сохраняет для Undo/Redo. Существующие `registerCommand` остаются совместимы, но после успешной валидации очищают историю до запуска executor и выступают undo-barrier, включая случай ошибки executor.
- Audit включает Undo/Redo и связывает их с ID/version команды, созданной транзакцией. История и audit остаются только в памяти; Scene Model, persistence, лимит истории, multi-user conflicts и интеграционные команды появятся позднее.

## D019 — Редактор 02.1
Optional ASTRO_BUILD_EDITOR использует локальные зависимости 00.3. project.astro хранит версию и UTF-8 имя. Создание в существующем каталоге запрещено. Renderer пока дублирует прототип; Scene Model не реализован. См. reports/02.1.md.

## D020 — Безопасное создание проекта

- Дата: 2026-11-13. Основание: исправление риска повреждения существующих файлов в узком исправлении 02.1.
- Создание допускается только в новом каталоге. `project.astro` полностью записывается во временный файл в этом каталоге и публикуется через Windows `MoveFileExW` с `MOVEFILE_WRITE_THROUGH`, без разрешения замены. При ошибке удаляется только временный файл; каталог удаляется только если он остался пустым.
- Чтение проверяет формат до изменения активного проекта. Неверная версия, синтаксис или ошибка доступа сохраняют прежнее состояние UI.
- Это ранний Windows-прототип, а не финальный межплатформенный файловый контракт. Поведение файловой системы вне проверенной Windows-среды отдельно не сертифицировано.

## D021 — Минимальная модель сцены 02.2

- Дата: 2026-11-13. В рамках 02.2 Scene содержит монотонный scene-local Entity ID, UTF-8 имя и необязательную ссылку на родителя; операции проверяют валидность ссылки и запрещают циклы.
- Удаление сущности с детьми по умолчанию отклоняется; рекурсивное удаление требует явного флага. Это предотвращает случайное удаление поддерева через обычный delete.
- Scene View сейчас является графическим structural preview на основе отношений parent/child. Она не задаёт координаты мира и не является runtime renderer; Transform/компоненты и отображение игровых сущностей остаются будущей работой.
- ID действуют только в памяти Scene и не переживают её пересоздание/перезапуск. Persistence, миграции, стабильные сохранённые ID, Command API и Undo/Redo остаются вне 02.2.

## D022 — Schema properties и Scene command 02.3

- Дата: 2026-11-13. Inspector не меняет Scene напрямую: он выполняет `entity.set_property` v1 через `CommandRegistry`, тот же адаптер доступен тестам и другим клиентам.
- В текущей минимальной модели схема содержит `name` (обязательный редактируемый UTF-8 string, default `Entity`, максимум 240 байт) и `id` (read-only unsigned integer в диапазоне ненулевых EntityId). Новые типы полей добавлять вместе с моделью, валидаторами, UI editor и тестами; variant сейчас не означает, что произвольные типы уже поддерживаются.
- Команда регистрируется как non-transactional и создаёт Undo barrier; отмена/повтор интегрируются отдельно в 02.4.
- Для согласования с предсобранными статическими SDL3/bgfx библиотеками MSVC runtime задаётся единообразно на верхнем уровне CMake (`/MT` или `/MTd` по конфигурации), включая engine и тесты. Новых внешних зависимостей нет.

## D010 — Минимальный формат сцен Astro Scene v1 (02.4)

Сцены редактора хранятся в UTF-8 текстовом формате `ASTRO_SCENE 1` с явно записанными Scene ID, Entity ID, parent ID и именами. Формат покрывает только существующую Scene/Entity модель и не является полной сериализацией компонентов или Save System. Запись выполняется через временный файл в том же каталоге и атомарную замену (Windows: MoveFileExW). Открытие сначала полностью разбирает и проверяет временную модель, затем заменяет активную сцену; ошибки диагностируются без подмены активного состояния. Миграций, резервных копий и crash-recovery временного файла пока нет.
## D011 — Transform как обязательный базовый компонент (03.1)

Каждая сущность получает ровно один типизированный `astro.Transform` версии 1 при создании. Transform хранит локальные position, Euler rotation (градусы) и scale в double-векторах; значения должны быть конечными. `ComponentId` уникален в пределах сцены, сохраняется на диске и не переиспользуется при удалении в текущем экземпляре сцены. Не добавлять фиктивный универсальный контейнер произвольных компонентов: будущие типы добавлять с отдельной схемой и явной диагностикой неизвестных данных. Формат файла поднят до v2, v1 мигрирует к default Transform.

## D012 — Локальный Asset Registry без image dependency (03.2)

AssetId назначается монотонно и сохраняется в проектном `.astro/assets.index`; источник идентифицируется относительным UTF-8 путём под `Assets/`, кэш производных данных отделён в `Library/Assets`. PNG/JPEG получают ограниченную проверку структуры/размеров и SHA-256 без добавления внешней библиотеки или платформенного codec API. Первая версия сохраняет byte-identical encoded copy; перекодирование/оптимизацию и декодирование пикселей не объявлять готовыми функциями. Изменение source требует явной команды reimport, сохраняющей ID. Пути и symlink должны разрешаться внутри проекта; malformed index не перезаписывать молча.

## D023 — SpriteRenderer и ранний Scene View preview (03.3)

- SpriteRenderer остаётся отдельным типизированным компонентом со стабильным `ComponentId`, `AssetId` и флагом видимости; AssetRegistry не зависит от Scene, а Scene использует public `AssetId` из `astro_assets`.
- `entity.set_sprite_renderer` v1 является транзакционной командой, поддерживает Undo/Redo. Astro Scene v3 записывает компонент; загрузчик сохраняет чтение v1/v2, а сохранение мигрирует при явной записи.
- Windows Editor использует системный WIC как локальный декодирующий adapter и BGFX для текстурного preview, без добавления сторонней библиотеки. Лимит preview — 16384 на сторону и 64 млн пикселей.
- Изображение показано как preview выбранного компонента в Inspector; это не пространственная сцена, runtime renderer, кроссплатформенный decoder или гарантированный golden-image результат.

## D024 — Prefab assets, instances and updates (03.4)

- Prefab assets use a project-local numeric ID and a versioned UTF-8 `.astro-prefab` file; Transform/SpriteRenderer data is copied into the asset, nested links remain PrefabId references.
- Nested prefab links expand beneath their owning node. Repeated PrefabId in the active expansion stack is a cycle; depth is limited to 64 and expansion to 100000 Scene entities. Local hierarchy depth is limited to 256.
- Scene v4 stores each instance root, flattened source key → EntityId mapping and explicit typed Name/Transform/SpriteRenderer overrides. v1/v2/v3 scenes still load and upgrade only on explicit save.
- Update computes/prevalidates the new graph on a Scene copy, keeps IDs for source keys that still exist and applies base values before overrides. Overrides whose source key disappears reject the update; the live Scene stays unchanged. Removed prefab nodes with non-prefab children also reject rather than delete those children.
- `prefab.instantiate`, `prefab.set_override`, and `prefab.update_instance` are transactional. Undo/Redo keeps full Scene snapshots for this early implementation; memory use scales with scene size. Editor authoring UI and merge of simultaneous source/instance edits remain future work.

## D025 — Asset dependency graph and derived package prototype (03.5)

- Dependency edges are explicit AssetId links; scene/prefab dependency extraction is not automatic yet. Cycles and invalid IDs are rejected; the sorted transitive closure is deterministic.
- The initial package preserves validated encoded PNG/JPEG bytes and metadata, tags a target profile in the content hash, and writes beneath ignored `Library/Derived`. Cache reuse requires exact byte equality; damaged output is atomically replaced.
- Target tags do not claim platform codec compatibility. Texture transcoding, platform toolchain builds, runtime package loading, and full game export remain separate work. No external dependency was added.

## D026 — Runtime Session and isolated Play Mode (04.1)

- Play owns a deep value-copy of the authoring Scene. Runtime callbacks cannot mutate editor state unless external state is captured by the callback; that remains the caller's responsibility.
- Runtime Session controls fixed-tick dispatch and explicit Stopped/Playing/Paused/Faulted states. Manual single-step is legal only while paused and advances the simulation clock once.
- A callback error faults the session and retains the runtime Scene for inspection; Stop clears it. Rendering, input, editor controls, and actual game systems are not implied by this controller.

## D027 — Input Mapping and SDL3 adapter (04.2)

- Action bindings and state aggregation stay independent of SDL/window APIs; input adapters submit physical state to `InputMapping`.
- The initial SDL3 adapter translates event-queue key scancodes, mouse buttons/motion/wheel, and gamepad buttons/axes/removal. It remains optional with the editor target and is not linked into headless core.
- No persistent user profile, remapping UI, device enumeration/ownership, vibration, or non-Windows adapter is claimed. Device behavior was validated with synthetic SDL events only.

## D028 — Runtime Camera Rig pose and follow contract (04.3)

- Camera mode presets produce a tested world-space pose convention; target follow uses the target entity's stored Transform position plus world-space offset.
- Smoothing is exponential and frame-delta aware; optional bounds clamp both desired and resulting camera position. Zoom is a scalar range, not a projection/FOV contract.
- No look-at except FirstPerson's rotation inheritance, world-transform hierarchy composition, projection matrices, Scene camera component, serialization, renderer adapter, or editor UI is claimed. The rig is useful/testable pose logic but not a rendered camera feature.

## D029 — Runtime interaction behaviors and typed events (04.4)

- `InteractionSystem` stores code-defined runtime behavior bindings keyed by EntityId; it reuses the typed core EventBus for sequenced Requested/Activated/Rejected events rather than adding a second event transport.
- Eligibility checks actor/target existence, enabled state, Transform and Euclidean range before invoking the behavior callback on the supplied runtime Scene.
- Bindings are not Scene-serialized and are not editor-authored; physics triggers, UI prompts, input routing and undo are not implied. Callbacks and post-activation observers can cause non-transactional side effects; the caller owns the runtime copy and recovery policy.

## D030 — Minimal dependency-free runtime 2D physics (05.1)

- `PhysicsWorld2D` is a separate optional C++20 target and introduces no third-party physics dependency. 2D body/collider data remains runtime-owned and binds to one SceneId; callers step it inside RuntimeSession using the play Scene copy.
- The first verified slice uses static/dynamic bodies, gravity, axis-aligned boxes in XY, discrete pairwise overlap checks, basic positional correction and EventBus Enter/Stay/Exit notifications. Triggers report overlaps without resolving them.
- This is deliberately not a full physics engine. Rotation, mass/material response, CCD, joints, persistence, editor authoring and rendering are deferred; high-speed tunneling and O(n²) scaling are known limitations.

## D031 — Backend-neutral Game UI with optional SDL3/ImGui adapter (05.2)

- `GameUiModel` is always available as a dependency-light runtime state/controller API. It coordinates menu/pause transitions with RuntimeSession, exposes HUD/settings data, and emits typed notifications.
- Visual presentation is an optional `ASTRO_BUILD_GAME_UI_DEMO` target using existing SDL3 and Dear ImGui cache. It is independent of Editor source/target and is not linked into the default headless runtime.
- HUD and settings remain in memory. Audio, persistence, localization, gamepad UI navigation and non-Windows adapters are not claimed. Observer failure after a transition does not roll back the model/session state.

## D032 — Minimal puzzle rules, scalar animation sampling, optional SDL audio cues (05.3)

- Puzzle rules and scalar animation sampling live in dependency-light `astro_runtime_puzzle`; state is currently runtime-only. Match-three uses orthogonal swaps, run clearing, gravity/refill, seeded randomness, cascade scoring and score thresholds. It is not yet connected to a visual board or project/scene serializer.
- The audio adapter reuses SDL3 already present in the optional demo profile. It queues short generated sine cues to the default device rather than introducing an audio library or claiming a general asset playback/mixing system. Failure to open audio is reported by the demo and does not block gameplay UI.
- SDL audio smoke uses the dummy driver; it validates queueing through SDL's output stream, not audible speaker output. No new external dependency or installation was needed.

## D033 — Explicit selective SaveProfile and bounded Save v1 slots (05.4)

- SaveProfile is opt-in and starts with no fields. In v1 the caller names Entity IDs and property IDs directly; no whole-scene default, tag/group inference, or implicit persistence is permitted. Changing the selected field set changes a profile fingerprint, and compatibility requires matching profile ID/version/fingerprint, game version and Scene ID.
- `SaveStore` receives an application-owned user-data root and generates numeric slot filenames. It uses a bounded UTF-8 text format, CRC32 for accidental corruption detection, unique temp files and atomic replacement. A prior parseable slot is atomically preserved as `.bak`; load falls back to it if primary data is malformed. CRC32 is not anti-cheat protection.
- Save v1 supports entity name, scalar Transform values and SpriteRenderer visibility only. Unsupported gameplay components and property migrations are rejected/deferred. Missing entities/components are reported and skipped; restore is applied to a Scene copy before publication.
- No JSON dependency, editor UI, automatic trigger, platform-specific root discovery, checkpoint, respawn, or migration framework was added. `ASTRO_BUILD_SAVE_RUNTIME` gates the library for non-test builds.

## D034 — Runtime-only checkpoint triggers over explicit save profiles (05.5)

- Checkpoint definitions bind to a Scene EntityId and XY AABB centered on that entity's stored Transform position. Detection is edge-triggered (outside → inside); hierarchy/world transforms, Z extent, physics query, and persistent component serialization are not part of v1.
- Runtime condition is a caller-supplied `Result<bool>` callback. A checkpoint selects either `RespawnOnly` or `SaveSelectedProfile`; the latter must name a registered profile and valid SaveStore slot and saves before activation becomes current. OneShot becomes consumed only after a successful activation; Repeatable can trigger again after leave/re-enter.
- Respawn is explicitly invoked by host gameplay. Default policy restores only the configured checkpoint/fallback position. SelectedProfile restoration is a separate opt-in policy and is allowed only for a SaveSelectedProfile checkpoint; it applies into a Scene copy before the checkpoint spawn position is assigned.
- Conditions and EventBus observers may have external side effects that are not rolled back. Respawned event observers run after the runtime Scene has been replaced. Definitions and active state are runtime-only and must be registered against the Play Scene. No editor authoring, death detector, visual/audio behavior, health/inventory rules, or no-code condition graph was added.
- `ASTRO_BUILD_CHECKPOINT_RUNTIME` gates the target for non-test builds and also enables its SaveStore dependency; no new third-party dependency was added.

## D035 — Windows package for the current Game UI prototype (05.6)

- `ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE` is opt-in and currently requires Windows x64/MSVC plus `ASTRO_BUILD_GAME_UI_DEMO=ON`. It uses CPack ZIP; default/headless builds remain unaffected.
- The archive contains the statically linked `astro_game_ui_demo.exe`, a runtime README, and the SDL3/Dear ImGui upstream license texts from the existing local dependency cache. Missing license texts fail configuration instead of silently distributing an incomplete package. No DLL is currently required in the Release archive; OS system libraries remain platform requirements.
- A CTest smoke builds the archive, extracts it into a fresh unique directory and starts the packaged executable with `--smoke` from that directory. It does not use a project/source-tree resource. The Release ZIP is a package of the current Game UI prototype only.
- AstroEngine's own license remains undecided. This package does not establish project licensing or satisfy third-party license review for all dependencies. The visible multi-level puzzle and connected SaveProfile/checkpoint progression remain absent, so Stage 05 acceptance is still open.

## D036 — Keep Visual Logic 06.1 as a pure typed dataflow prototype

- The first Visual Logic iteration is an optional C++20 graph/evaluator with typed Number/Boolean/String pins, deterministic evaluation, bounded node/text sizes, and a synchronous step debugger. It has no new third-party dependencies.
- The editor uses typed menus for connections to keep this substage small. Graph files, undo, execution-flow/branch nodes, runtime bindings, gameplay commands, Scene mutation, and drag-wire authoring are deferred; no UI action should imply these are available.
- Keep the evaluator independent of `CommandRegistry` and Scene until the graph model, persistence, and transactional semantics can be designed and tested together. This prototype does not meet Stage 06 acceptance by itself.

## D037 — Bound the first UI Editor to a previewable in-memory document (06.2)

- The UI document model supports stable local element IDs, Panel/Text/Button elements, Dark/Light theme tokens, responsive anchors and pixel offsets, and typed bindings from `Entity.Name` to Text or `SpriteRenderer.Visible` to Visible.
- Binding refresh reads Scene values into the UI document and never writes to Scene. It validates all bindings before applying refreshed values, so a stale source does not partially update the document.
- The first authoring panel is an in-memory editor prototype. Versioned UI-file persistence, gameplay/runtime rendering, more binding sources, navigation/input, localization, nested interactive layout editing, and custom theme authoring are deferred. This does not satisfy full Stage 06 acceptance.
- No additional third-party dependency is introduced; the Editor panel uses the existing ImGui integration.

## D038 — Separate animation sampling from puzzle runtime and expose an editor preview (06.3)

- `AnimationTrack` and `AnimationTimeline` live in the optional `astro_runtime_animation` target. `ASTRO_BUILD_PUZZLE_RUNTIME` retains a public dependency on that target for compatibility; `ASTRO_BUILD_ANIMATION_RUNTIME` enables animation without the Puzzle module.
- Timeline authoring is limited to one bounded scalar track with strict key ordering, linear interpolation, scrub and simple play/pause. This is not a general animation graph or Scene-property binding system.
- Camera Presets reuses `CameraRig` modes and validation; the panel previews computed pose values only. It does not create a camera component or render a view.
- Both panels use existing editor dependencies; no new external library is added. State remains in memory until a versioned animation document format is designed.

## D039 — Keep Save System authoring explicit and runtime-compatible (06.4)

- Editor groups are explicit entity-ID sets; profile group rules flatten to the existing `SaveProfile` entity/property pairs before runtime use. No scene-wide or tag-inferred saving is introduced.
- The panel offers only Save v1 properties that the runtime can actually store. It validates component availability before compilation and reports unsupported/missing bindings instead of silently omitting them.
- Checkpoint configuration is validated by `CheckpointSystem`; the XY view is a Transform-based authoring preview. Restore testing uses an isolated Scene copy and a unique OS temporary directory.
- Profile/group/checkpoint authoring remains in memory in 06.4. Persisting it requires a versioned project document format and migration policy, so it is not added implicitly to Astro Scene v4 or player save slots.
- No external dependency is added. This bounded editor work does not complete Save System product requirements such as tags, templates, health/inventory, conditions, or respawn policies.

## D040 — Add a bounded 2D canvas before authoring game templates

- `Canvas2D` stores backend-neutral filled-rectangle commands, explicit RGBA colors, and layer order; camera projection is independent of SDL. The optional SDL3 adapter owns no window/renderer resources.
- A standalone Play preview composes existing `InputMapping` and `RuntimeSession`, updates only a runtime Scene copy at the fixed timestep, and demonstrates SDL rendering/input without adding a heavyweight dependency.
- This is a geometry-only foundation and does not establish SDL3 as the final engine renderer. It does not render Scene/SpriteRenderer assets or provide editor Play UI, physics controller, or game templates. Do not mark 06.5 complete based on this experiment.

## D041 — Connect root Scene sprites through an asset-resolving render boundary

- `astro_scene_render2d` converts visible root `SpriteRenderer` components into backend-neutral Canvas2D sprite commands. It reads dimensions from `AssetRegistry`, applies XY position/scale and Z rotation, and rejects missing assets, invalid transforms and child sprites instead of silently misrendering unsupported hierarchy transforms.
- `SDLCanvas2DRenderer` accepts a caller-supplied AssetId-to-texture resolver; the renderer does not own decoded assets. The Windows sample uses a bounded WIC BGRA decoder and caches SDL textures for the renderer lifetime. Source images remain in Assets; decoding reads the validated Library cache.
- The WIC decoder is platform-specific and Windows-only. Future platform adapters must supply their own decoder. Parent transforms, camera components, editor preview integration, streaming, batching and template gameplay remain deferred; this follow-up does not complete stage 06.5.

## D042 — Compose 2D hierarchy as affine textured quads and preview existing projects read-only

- Each sprite's local rectangle is transformed through the root-to-entity XY chain with `T * Rz * S` composition. Four world-space corners are kept in the backend-neutral draw command and SDL3 renders them with textured geometry, preserving shear caused by nested rotations and non-uniform scales. X/Y Euler rotations and Z position are outside this 2D view.
- `astro_2d_playground --project <directory> --scene <relative-file>` loads Astro Scene v1–v4 and an existing AssetRegistry. The scene path must resolve inside the canonical project root. Preview requires existing non-symlink `Assets`, `Library/Assets`, `.astro`, and `.astro/assets.index`; this avoids `AssetRegistry::open` creating a missing project skeleton. Scene, source, registry and cache are read-only during preview; stale/missing cached images fail with diagnostics rather than being rebuilt.
- Project preview auto-fits visible sprite bounds. It is standalone and Windows-only while WIC is the only image decoder. It does not add Editor Play UI, project mutation, asset rebuild, camera components, scene hierarchy editing, or game templates.

## D043 — Initial Scene/Game View integration (superseded by D044)

- Keep the existing bgfx/ImGui editor backend. Render each view into a bounded SDL software surface and upload BGRA pixels as a bgfx texture for ImGui; this connects the established Canvas2D/SceneSprite/SDL path without replacing editor rendering.
- Scene View keeps its hierarchy map as a separate tab. Game View displays a disposable `RuntimeSession` Scene copy with Play/Pause/Resume/Step/Stop; the Scene remains unchanged by Play. Until gameplay modules are deliberately connected, the fixed-step callback is empty.
- This CPU copy is a bridge with measurable per-frame cost, not the final GPU renderer. The editor renders SpriteRenderer assets only and has no selection overlays or non-Windows WIC decoder.

## D044 — Render Scene/Game View directly into bgfx framebuffers

- Benchmarking at 960x540 with 1000 sprites measured 7.995 ms Scene View and 7.378 ms Game View CPU p50, before the bgfx frame completes. The current pair submits 4,147,200 BGRA bytes per frame (about 237.3 MiB/s at 60 FPS); `bgfx::copy/updateTexture2D` CPU submission alone was 0.69–0.73 ms per pane. This excludes actual GPU transfer completion and is not a GPU-time measurement.
- Implemented as two bgfx framebuffer views. The adapter converts the backend-neutral sprite corners to transient vertex/index buffers, samples textures already uploaded by Editor, batches consecutive sprites sharing a texture, and renders both views before ImGui. This removes SDL software rendering and full-frame CPU uploads. SDL remains the independent runtime adapter.
- On the recorded final run with the same synthetic scenes, 1000-sprite CPU p50 is 4.809 ms Scene / 4.782 ms Game, versus 7.995 / 7.378 ms on the CPU bridge. Paired p50 falls from 15.373 to 9.591 ms (37.6%); conservative paired p95 sum falls from 18.271 to 11.408 ms (37.6%). bgfx GPU frame timing reports 0.074 ms p50 / 0.135 ms p95 for both offscreen views. Full-frame host upload is zero; 1000 sprites submit 184,000 bytes of transient vertex/index data for both views.
- The 1000-sprite CPU prep/vertex build is now the main measured cost (~5.3–5.6 ms per view). Benchmarks are synthetic and local to D3D11/MSVC Debug, and do not include the full ImGui composition in the CPU totals. Visual testing on another GPU/backend and broader alpha/resize golden coverage remain useful follow-ups.

## D045 — Validate Scene/Game framebuffer output with pixel predicates (06.11)

- The Editor smoke captures a temporary bgfx backbuffer TGA after resizing the window and validates minimum pixel counts for opaque red, opaque blue and alpha-blended green. This covers texture switching, blend state, framebuffer recreation and presentation through ImGui without checking in a GPU-specific golden image.
- Color ranges are used instead of exact image hashes because GPU/backend/color-pipeline differences can change edge pixels. The screenshot and generated test project are removed after the assertion; command logs are retained.
- The synthetic 960x540 D3D11 Debug benchmark includes up to 10,000 sprites, within the current 16-bit transient index ceiling. Dense-scene CPU cost is a measured limitation, not an approved frame budget or a claim about Release performance.
- Release renderer libraries build, but full Editor Release linking requires `bgfxRelease.lib`; the current dependency cache contains only `bgfxDebug.lib`. No dependency build or download was started in this substep.

## D046 — Use indexed, cached world transforms for dense 2D scene composition (06.12)

- Profiling showed that `Scene::transform(EntityId)` calls the Scene's linear `find`, so composing each sprite independently made a flat N-sprite scene O(N²). Parent resolution also used that linear lookup.
- `appendSceneSprites` builds a per-call EntityId-to-vector-index map and a vector-backed world-matrix state/cache. Parent affine transforms are composed once and reused by descendants; cycle/missing-parent/missing-Transform and finite-value diagnostics remain. The cache is scoped to one composition call, so editor edits cannot leave stale transforms across frames.
- For exact zero Z-rotation, local matrices avoid sin/cos. Axis-aligned matrix columns use absolute values instead of `hypot` only when the other coefficient is exactly zero; the general affine/shear path retains `hypot` and `atan2`.
- No Scene public API or serialized format changes. Existing hierarchy integration and full optional CTest pass. D3D11 Debug 10k CPU p50 falls from 142.218/141.326 ms to 26.086/24.348 ms for Scene/Game; optimized Release measures 4.913/4.682 ms. Measurements exclude ImGui composition and vsync.

## D047 — Report the complete VSync-limited Editor frame separately from view CPU cost (06.13)

- `astro_editor --benchmark` measures representative temporary scenes through SDL event polling, editor UI composition, both framebuffer views and `bgfx::frame()`. It reports 24 warm-up + 48 sampled frames per case and separates pre-frame Editor CPU, Scene/Game view CPU, bgfx frame call and total host wall time.
- Keep VSync enabled to measure the real interactive Editor path. On this D3D11 Release run, p50 wall time is ~16.6 ms and p95 ~17.3–17.6 ms; the host frame-call wait is ~14.6–14.8 ms. These are observed paced frame times, not uncapped throughput or pure GPU execution time. Driver BGFX latency counters were unavailable, and the reported GPU timer interval is not used as a standalone GPU workload claim.
- Benchmark scenarios are deterministic, generated under the benchmark-owned temporary project, and do not read or modify user game assets. They approximate common 2D content patterns but are not representative of a real game project; hardware, window compositor, background load and driver affect the result.

## D048 — Build SDL 2D adapters when Editor tests consume the playground (06.14)

- The 2D playground is added when either its standalone option is enabled or Editor testing is enabled. In the latter configuration, its runtime SDL renderer and WIC image adapter are required link targets even when `ASTRO_BUILD_2D_PLAYGROUND=OFF`.
- Derive the internal adapter-build condition from `ASTRO_BUILD_2D_PLAYGROUND` or `(ASTRO_BUILD_EDITOR AND BUILD_TESTING)`. This keeps standalone module opt-in while ensuring the enabled Editor smoke/integration consumer gets real CMake targets rather than unresolved library-name strings.
- Reconfigure and build all targets, then run the full Debug CTest suite to verify the link graph. Recorded result: build passed and **37/37** tests passed. See [06.14 report](reports/06.14-debug-link-and-real-scene-profile.md).

## D049 — Snapshot Puzzle runtime state without coupling the rules module to storage (06.5)

- `PuzzleGame` exposes a validated value snapshot containing board/rules/progress and the serialized `std::mt19937` engine state. Restore validates bounds, tile palette values, and complete RNG parse before publishing a game value.
- Keep filesystem format, user-data location, checksums, slots, and explicit SaveProfile selection in SaveStore; the puzzle rules module does not write files or silently persist the whole board.
- This is a storage integration seam only. It does not claim project save/load, editor authoring, or a complete T01 template. Unit + full Debug CTest passed; see [06.5 snapshot report](reports/06.5-puzzle-snapshot.md).

## D050 — Add explicitly selected typed custom values to SaveStore v2 (06.6)

- Save profiles may declare ASCII custom keys and their string/double/bool type. A save call must provide exactly the selected keys with matching types; this keeps profile selection explicit and prevents silently persisting arbitrary runtime data.
- Preserve Scene-only calls and `ASTRO_SAVE 1` for profiles with no custom values. Custom data uses `ASTRO_SAVE 2`; the parser continues to read v1. The profile fingerprint includes custom key/type selections.
- Bound custom keys to 128 bytes and strings to 64 KiB, reject non-finite numbers, and validate all custom records before publishing either the loaded Scene or caller state.
- This is a generic value map seam; it does not serialize PuzzleSnapshot or arbitrary components automatically. Integration and compatibility checks: [06.6 report](reports/06.6-save-custom-fields.md).

## D051 — Persist Puzzle snapshots through a separate optional SaveStore adapter (06.7)

- Keep `astro_runtime_puzzle` independent of files and `astro_runtime_save`. Add `astro_runtime_puzzle_save` only when tests are enabled or both modules are explicitly requested.
- The adapter serializes a `PuzzleSnapshot` into one profile-selected string key (`puzzle.snapshot` by default); it does not select fields or save a whole Scene implicitly. Other selected custom values remain explicit inputs/outputs.
- Give the Puzzle payload its own version (`ASTRO_PUZZLE_SNAPSHOT 1`) inside the existing checksummed SaveStore v2 slot. Unknown versions and malformed payloads fail; no migration is claimed.
- Load into a Scene copy and publish it only after SaveStore validation, payload parsing and `PuzzleGame::restore` all succeed. See the integration checks in [06.7 report](reports/06.7-puzzle-save-adapter.md).

## D052 — Run Puzzle as an optional Game View sample mode (06.15)

- Keep Puzzle rules in `astro_runtime_puzzle`; link the Windows Editor to that target only when it exists. `Play Puzzle` is available in test builds or when `ASTRO_BUILD_PUZZLE_RUNTIME=ON`, and the runtime target remains optional.
- Render the board in Game View, queue adjacent-cell selections there, and execute swaps from the `RuntimeSession` fixed-step callback. Pause holds queued input and Step executes one queued move.
- Keep Puzzle preview state separate from the authored Scene and from generic Scene Play. This is not a serialized Scene component, project template, or automatic progress-save policy.
- `editor.smoke` covers board rendering, accepted moves, Pause/Step and authoring Scene isolation. See [06.15 report](reports/06.15-puzzle-game-view.md).

## D053 — Expose explicit Puzzle snapshot slots in Game View (06.16)

- Reuse `astro_runtime_puzzle_save`; do not implement another encoder or persist Puzzle state as Scene fields. The Editor profile selects only the string key `puzzle.snapshot`.
- Offer manual slots 0–9. No automatic writes. For the built-in Editor preview, obtain the user-writable root through `SDL_GetPrefPath` and append `Saves/PuzzlePreview`; show the resolved directory. Do not store player progress in the project checkout.
- Keep the adapter optional: Editor links it only when the target exists; test configuration builds it automatically, and Editor + Puzzle explicitly enables it for non-test builds.
- The graphical smoke substitutes a unique temporary test-owned directory and removes it. It saves a board, mutates it, loads the selected slot, verifies exact snapshot restore, then continues Pause/Step regression. See [06.16 report](reports/06.16-puzzle-save-game-view.md).

## D054 — Store Puzzle preview SaveProfile and user-data subdirectory per project (06.17)

- Keep `project.astro` v1 unchanged. Store Editor-only Puzzle settings in optional `.astro/puzzle-save.settings` v1 with atomic replacement and strict parsing.
- Store profile ID/schema and a relative UTF-8 user-data subdirectory. Reject absolute/rooted paths and parent traversal; keep progress slots outside the project checkout under `SDL_GetPrefPath`.
- Default to profile `puzzle-preview` v1 and a deterministic folder derived from normalized project root. This distinguishes projects without a project-format migration; settings are persisted on Apply or the first Save.
- Keep the SaveProfile field explicit and narrow: only string `puzzle.snapshot`. Profile/schema edits do not delete or migrate old slots. The legacy shared `Saves/PuzzlePreview` path can be selected manually; no automatic file move occurs.
- `editor.smoke` checks default path isolation, settings round-trip, rejection without overwriting valid settings, settings UI visibility and existing slot round-trip. See [06.17 report](reports/06.17-project-puzzle-save-settings.md).

## D055 — Keep Puzzle level authoring separate from player progress (06.18)

- Model authored Puzzle content as `PuzzleLevelDefinition`, not as Scene entities or SaveStore slots. Keep the existing seeded PuzzleGame creation API intact and add a validated overload for exact initial tiles.
- Store project content under `Puzzle/Levels/<safe-ID>.astro-puzzle-level` with an explicit v1 format, strict bounds/UTF-8/tile validation and atomic replacement. Unknown versions fail with a diagnostic; no migration is claimed.
- Do not allow the current preview SaveProfile to load/save while an authored level runs: the slot fingerprint identifies profile/schema, not the Puzzle level. Keep existing sample slots available in the built-in sample mode; do not move or delete any user data.
- Scope the first GUI to one editable draft and its cells/parameters. Level library/campaign, delete/duplicate workflow, Undo for draft edits and per-level progress are future work. See [06.18 report](reports/06.18-puzzle-level-authoring.md).

## D056 — Bind Puzzle progress slots to authored level IDs (06.19)

- Keep the SaveStore container and `PuzzleSnapshot` payload unchanged. For authored levels, explicitly select custom string key `puzzle.level.id` and validate the saved value against the active level ID before publishing loaded state.
- Partition Editor slots under `PuzzleLevels/<ID>` in the project's existing SDL user-data namespace. Preserve the built-in sample profile and directory and do not move/delete existing user data.
- Reject unsafe/malformed IDs; level names do not determine storage identity. This provides per-level slot isolation, not a campaign/progression graph, automatic saving, or schema migration. See [06.19 report](reports/06.19-level-scoped-progress.md).

## D057 — Add a safe project Puzzle level catalog (06.20)

- Keep `ASTRO_PUZZLE_LEVEL 1` and the stable-ID filename layout unchanged. Catalog enumeration is bounded, sorted by ID, restricted to safe regular files inside the project, and returns per-entry diagnostics so one malformed file does not hide readable levels.
- Add create-only publication for new/duplicate IDs. Preserve the existing explicit Save/update behavior for an already opened same-ID draft. Duplicate and rename reject occupied IDs rather than replacing files.
- Rename writes and validates the new definition, rechecks that the original has not changed, and removes the old path only after the new file is available; on failure it attempts rollback and returns an error. Editor asks for confirmation. The old progress-save namespace remains untouched and is not migrated.
- Opening another level or resetting a changed draft requires explicit discard confirmation. No bulk deletion, campaign progression, or undo journal is added. Limits and tests are in [06.20 report](reports/06.20-puzzle-level-catalog.md).

## D058 — Specify a versioned Puzzle campaign document before implementing progression (06.21)

- Use a separate UTF-8 text file `Puzzle/Campaigns/<campaign-id>.astro-puzzle-campaign` with an explicit ordered list of stable level IDs and a separate `START` reference. List order is for presentation; it does not imply transitions.
- Keep v1 rules deliberately small: ordered `SCORE_AT_LEAST` branches followed by exactly one `DEFAULT` to another member level or `END`. Evaluate using the completed level's final score when the host requests one transition. Unknown conditions fail validation; no scripting callbacks or external state are implied.
- Require all references to resolve within the campaign and all levels to be reachable from START. IDs remain stable; future rename must not leave dangling campaign references. No campaign parser, runtime behavior, or Editor UI is claimed in this design-only slice.
- The complete proposed grammar, example, bounds, and future acceptance tests live in [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md). The proposal needs technical review during implementation; v1 has no migration yet.

## D059 — Parse and persist the v1 Puzzle campaign format without implying progression (06.22)

- Keep the D058 text grammar and its explicit order/start/score/default model. Implement strict UTF-8 parsing/serialization and structural validation in `astro_runtime_puzzle`; unknown versions, conditions, malformed records, unsafe IDs, bad references, unreachable levels, and out-of-bound documents fail with diagnostics.
- `PuzzleCampaignFile` owns only project content at `Puzzle/Campaigns`; it validates authored level references and path containment and publishes via sibling temporary + atomic replacement. Player progress remains in the existing save system.
- This completes the data format contract only. It does not resolve score transitions, add Editor campaign UI, migrate schemas, or make level rename reference-aware. Tests and explicit limits: [06.22 report](reports/06.22-puzzle-campaign-parser.md).

## D060 — Resolve exactly one Puzzle campaign edge per completion request (06.23)

- Add a pure resolver over a validated campaign, completed level ID, and final score. Evaluate that source's descending score rules in declared order and return the first match; otherwise return its DEFAULT target.
- Represent `DEFAULT END` as a successful empty optional. Reject invalid campaigns and completion IDs that are not campaign members.
- Do not load the returned level or recursively consume another transition. The host owns loading and requests resolution again only after a later completion. Editor controls and automatic progression orchestration are not part of this substage; see [06.23 report](reports/06.23-puzzle-campaign-resolver.md).

## D061 — Make Puzzle campaign END terminal and preserve the result board (06.24)

- Add `PuzzleCampaignSession` as the host for authored Puzzle level data and a playable `PuzzleGame`. A successful move that reaches the current authored `targetScore` marks the level ready for explicit completion; the session resolves the active score and stages the next level/game before publishing them.
- If next-level read or game initialization fails, preserve the current completed board and allow retry after the asset is repaired.
- `DEFAULT END` transitions the session to `Ended`, retaining the final level and score for result UI. The ended session accepts no more moves or completion requests. Restart is explicit by creating a new session, which begins at campaign START. Do not implicitly restart or save progress here.
- The session loads Puzzle level files only; it does not load engine Scenes or integrate with Editor UI. See [06.24 report](reports/06.24-puzzle-campaign-session.md).

## D062 — Host Puzzle campaign sessions in Game View (06.25)

- Game View starts a `PuzzleCampaignSession` from the active project and an explicit campaign ID. Tile input enters the session only through the existing fixed-step runtime callback.
- Reaching a level target does not advance automatically. The view exposes Continue so users can inspect the completed board/score before the authored transition is resolved.
- `END` retains the final board/result in Game View; restart is explicit and creates a fresh session from START. Stop clears the active session.
- Campaign progress is not written to the standalone Puzzle SaveStore slots; do not imply persistence until a campaign save identity/format is designed. Campaign authoring remains file-based in this substage. See [06.25 report](reports/06.25-puzzle-campaign-game-view.md).

## D063 — Keep the campaign browser read-only and report broken level links (06.26)

- Add a bounded `PuzzleCampaignFile::list` API that distinguishes malformed documents from structurally valid campaigns with unavailable level assets. Preserve the parsed campaign definition when only an asset reference is broken so the Editor can explain the issue.
- Show campaign IDs/names, START, ordered level IDs, transition rules, and per-level asset status in a read-only Editor browser. Selecting a structurally readable campaign fills the Game View launch ID; launching still performs the stricter full reference validation.
- Do not rewrite project files during enumeration or diagnostics. Keep malformed files visible and cap directory enumeration; do not recurse or follow campaign symlinks. See [06.26 report](reports/06.26-puzzle-campaign-browser.md).

## D064 — Author campaigns through validated drafts and guard level renames (06.27)

- Extend the campaign browser with one explicit draft editor for ID/name, ordered level references, START, score thresholds, and DEFAULT/END targets. Confirm writes and route them through `PuzzleCampaignFile::write`; changing an ID writes a separate campaign and never removes the old one.
- Preserve unsaved draft edits unless the user explicitly confirms discard. Level rename checks every campaign: refuse when the old ID is referenced, when a campaign is unreadable, or when enumeration fails. The user updates and saves campaign references before renaming, then adds the new ID afterward. Do not perform multi-file rewrites or progress-slot migration implicitly.
- Physical UI click automation and campaign progress persistence are outside this slice. See [06.27 report](reports/06.27-puzzle-campaign-authoring-rename.md).
# D065 — Перевод Windows Editor через внутренний каталог (06.28)

- Поддержать English и Russian для интерфейса Windows Editor. Держать словарь внутри `editor/Localization.*`, без сторонней зависимости.
- Хранить выбор языка как версионированное пользовательское предпочтение SDL, отдельно от проекта и игрового содержимого; по умолчанию использовать English. Применять настройку сразу после Apply.
- Сохранять стабильные скрытые ImGui ID при переводе названий окон, чтобы переключение языка не ломало докинг.
- Не считать runtime/demo, API-диагностики, драйверные ошибки или пользовательский текст автоматически локализованными; эти границы явно документировать.
- Проверять прямые литералы UI-текста на наличие перевода, round-trip настроек и smoke отображения окна настроек на русском. См. [отчёт 06.28](reports/06.28-editor-russian-localization.md).

## D066 — Раздельные каталоги редактора и runtime UI (06.29)

- Сохранить локализацию Editor отдельной от игрового UI: проекты и игры не должны зависеть от Editor.
- В 06.29 `ui.language` использовал стабильные `en`/`ru` и действовал только в процессе demo. Это ограничение было снято в 06.30: язык хранится отдельно от проекта и игровых слотов согласно D068.
- Не переводить произвольный HUD/пользовательский текст. Известные ошибки GameUiModel и ImGui adapter переводятся точно; неизвестная runtime/Editor диагностика получает русскую метку с сохранением оригинальной технической детали.
- Не переписывать ошибки SDL/драйвера, пути или данные. Системный шрифт включать только при русской локали, чтобы сохранить существующий английский визуальный baseline. См. [отчёт 06.29](reports/06.29-runtime-and-diagnostics-localization.md).

## D067 — Универсальный AI-first профиль продукта

- Дата: 2026-11-14.
- Статус: принято как продуктовая цель; не является утверждением о текущем паритете функций с Unity, Godot, Unreal Engine или CryEngine.
- AstroEngine предназначен для модульной разработки 2D/3D-игр разных жанров с графическим редактором. AI-assisted authoring — ключевой пользовательский путь и отличие продукта, а не второстепенный чат-плагин.
- GUI, визуальная логика, AI и программные расширения используют согласованную модель и общий типизированный Command API. ИИ планирует, показывает diff, проверяет разрешения, применяет допустимые изменения и запускает тесты; рискованные действия требуют подтверждения и отката/транзакции согласно контракту.
- AI adapter/provider не обязателен для запуска Editor, Runtime и игры; не вводить привязку к одному провайдеру или постоянной сети. Безопасность, аудит, sandbox и ограничения доступа остаются обязательными требованиями AI-интеграции.
- Puzzle рассматривается как ранний vertical slice для проверки систем и сохранений, а не как конечный жанр движка. Подробное ТЗ обновлено в [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md), направление в [ROADMAP.md](ROADMAP.md), технические ограничения в [AI_INTEGRATION.md](AI_INTEGRATION.md).

## D068 — Сохранять язык runtime отдельно от проекта (06.30)

- Хранить язык SDL Game UI demo в versioned user preference `game-ui-settings.cfg`, рядом с SDL preference root; не включать локаль в project files или game save slots.
- Отсутствующий файл означает English. Неизвестная версия/значение не меняют модель настроек; показать диагностику и разрешить исправление выбором языка через UI.
- При выборе языка сразу обновить интерфейс и выполнить запись через временный файл с безопасной заменой. Ошибку записи показать в Game UI с сохранением исходной ошибки ОС.
- Smoke-тест не читает/не перезаписывает реальные пользовательские настройки, а использует уникальный временный каталог, созданный самим тестом.

## D069 — Начать AI Editor API как typed in-process фасад (09.1)

- Дата: 2026-11-14.
- Решение: начать локальный API с `astro::core::AIEditorApi` поверх CommandRegistry: версия API, детерминированный каталог зарегистрированных схем и typed validate/execute.
- Фасад не дублирует исполнение: валидация, аудит, транзакции и Undo/Redo остаются в исходном CommandRegistry. Каталог отражает только команды, фактически зарегистрированные данной конфигурацией.
- Не выбирать JSON или сетевой протокол до соответствующего подэтапа и технической оценки; не считать in-process фасад security boundary. Codex adapter, разрешения, preview/approval и sandbox остаются обязательными следующими задачами.
- Обоснование: сохраняется одинаковое поведение команд и не вводится неподтверждённая зависимость или открытый endpoint.
- Подробности и ограничения: [COMMAND_API.md](COMMAND_API.md), [AI_INTEGRATION.md](AI_INTEGRATION.md), [отчёт 09.1](reports/09.1.md).

## D070 — Встроенный C# editor и пользовательские скрипты

- Дата: 2026-11-14.
- Статус: принято как требование продукта P15; Editor отсутствует, runtime host пока только технический prototype (04.5–04.6).
- В Editor должна быть отдельная встроенная Code workspace для ручного редактирования C# игровых скриптов. Скрипты сохраняются в проекте, собираются с понятной диагностикой и запускаются runtime через документированный API/ScriptBehavior.
- C# — обязательный целевой язык пользовательских скриптов; это не утверждение, что в engine уже установлен или выбран .NET/Mono/compiler/runtime host.
- Способ host/compiler, bindings, hot reload, лицензии, платформенная доступность, размер, время сборки и модель безопасности требуют отдельного технического прототипа. AI-изменения кода показываются как diff и не исполняются без предусмотренных permissions/approval; скрипты без проверенного sandbox получают обычные права процесса.
- Разделение задач закреплено в [ROADMAP.md](ROADMAP.md): 04.5 оценивает/проверяет runtime, 06.6 реализует редакторскую Code workspace после проверки основы. Требования и критерии: [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md), [EDITOR_UX.md](EDITOR_UX.md), [GAME_SYSTEMS.md](GAME_SYSTEMS.md), [TESTING.md](TESTING.md).

## D071 — Кандидат C# runtime для прототипа 04.5

- Дата: 2026-11-14.
- Статус: прототип проверен на Windows x64; .NET 10 и способ hosting не утверждены для production.
- Для Windows desktop prototype оценивать .NET 10 LTS SDK **10.0.404** с официальным native hosting API (`nethost`/`hostfxr`) и framework-dependent managed component. До 04.5 SDK отсутствовал; Roslyn C# 4.14 + установленный runtime 8.0.21 выполнил только отдельный compile/run probe. .NET 8 уже завершил поддержку 2026-11-10; .NET 10 LTS поддерживается до 2028-11-14 ([политика поддержки](https://dotnet.microsoft.com/en-us/platform/support/policy), [релизы .NET 10](https://dotnet.microsoft.com/en-us/download/dotnet/10.0)).
- Основание: документация Microsoft native hosting требует C++ host, SDK headers/libraries и managed component; документированный `hostfxr`/`nethost` путь рассчитан на framework-dependent deployments ([native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)). Native AOT запрещает динамическую загрузку assemblies и runtime code generation, что ограничивает стратегию пользовательских динамических скриптов для mobile targets ([Native AOT limits](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/)).
- Источники .NET лицензируются MIT; распространяемые Windows .NET binaries подпадают под отдельные условия Microsoft .NET Library License. Перед поставкой движка и runtime пакетов требуется отдельный license/notice review ([.NET license information](https://github.com/dotnet/core/blob/main/license-information.md)).
- Риски/открытые вопросы: Android/iOS deployment and AOT, platform host adapters, runtime package size, hot reload, editor language-service integration, crash and memory boundaries, sandbox. Native in-process scripts выполняются с правами процесса и не являются sandbox.
- По явному разрешению пользователя официальный .NET 10.0.404 SDK ZIP был проверен по SHA-512 и распакован только в игнорируемый `build/04.5-dotnet-sdk`; системные PATH/registry не менялись. В CMake добавлена выключенная по умолчанию опция `ASTRO_BUILD_CSHARP_RUNTIME`; managed sample не использует NuGet-пакеты. Полный результат и ограничения: [04.5](reports/04.5-csharp-runtime-prototype.md).

## D072 — Ограниченный versioned C# ABI prototype (04.6)

- Дата: 2026-11-14.
- Статус: принят только как экспериментальный Windows x64 ABI v1; не обещает plugin compatibility и не выбирает .NET для production.
- `CSharpScriptUpdateV1` включает struct size, api version, EntityId, fixed delta и invocation-local `CSharpScriptApiV1` table. V1 callbacks читают/записывают Transform одной сущности runtime Scene; все значения проходят через `Scene::setTransform` validation.
- Принимается только точный размер struct и version 1. Callback/user-data pointer валиден только синхронно на время вызова fixed-step. Нельзя сохранять pointers, вызывать callbacks позже или считать код sandboxed.
- Сохраняемый `ScriptBehavior` отложен: нужны stable script asset IDs, schema, lifecycle, serialization/migration, diagnostics и multi-script ordering. Указатели/host runtime state не сериализуются.
- Контракт, статусы callbacks и покрытие тестами: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [отчёт 04.6](reports/04.6-versioned-csharp-runtime-api.md).

## D073 — Persistent ScriptBehavior и Scene v5 (04.7)

- Дата: 2026-11-14.
- Статус: сохранённая authoring-ссылка реализована; runtime lifecycle contract принят как целевой, его dispatch не реализован.
- ScriptBehavior v1 хранит ComponentId, Script AssetId, managed type name (UTF-8, 1–512 байт, без пробельных/control bytes) и enabled-флаг. Компонент имеет стандартную component schema version 1. Runtime/native pointers не сериализуются.
- Astro Scene v5 добавляет `astro.ScriptBehavior`. Reader продолжает принимать v1–v4, где script list считается пустым; открытие не переписывает файл, следующее сохранение пишет v5. Неизвестные типы/version/невалидные references диагностируются и не отбрасываются.
- Целевой lifecycle: resolve enabled scripts до Play; `OnCreate` один раз; `FixedUpdate` по фиксированному шагу; `OnDestroy` один раз в обратном порядке создания при Stop/rollback. Порядок — Scene entity insertion order и component insertion order. Сбой/недостающий asset должен завершать Play диагностикой, не пропускаться молча.
- На момент D073 AssetRegistry индексировал только изображения, поэтому сохранённый Script AssetId ещё не разрешался в source/assembly. D074 расширяет общий asset index для source/type/output mapping; lifecycle и Editor authoring остаются незавершёнными. Подробности: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт 04.7](reports/04.7-persistent-script-behavior.md).

## D074 — Script assets use the shared AssetId index (04.8)

- Дата: 2026-11-14.
- Статус: принят для optional Windows C# prototype; не является финальным дизайном asset pipeline.
- `AssetRegistry` остаётся владельцем numeric AssetId: script entries используют общий счётчик с изображениями. `.astro/assets.index` v2 добавляет script metadata; v1 читается, а файл мигрирует на v2 только при явной записи регистрации/reimport.
- Script record хранит source под `Scripts/`, managed type name, output paths `.dll` и `.runtimeconfig.json` под игнорируемым `Library/Scripts/`, и source SHA-256. Разрешение проверяет границы путей, файлы и свежесть source. Повторная регистрация того же source сохраняет AssetId и обновляет mapping.
- Registry не компилирует C# и не проверяет тип самостоятельно. `CSharpScriptHost::loadScriptAsset` явно разрешает AssetId и через hostfxr ищет entry point `FixedUpdate`; имя assembly выводится из имени DLL. Это не автоматический Scene lifecycle и не sandbox.
- Покрытие: `assets.registry.integration` (совместимость индекса/пути/hash) и `runtime.csharp_host.integration` (реальное разрешение и загрузка sample assembly). См. [FILE_FORMATS.md](FILE_FORMATS.md), [ASSET_PIPELINE.md](ASSET_PIPELINE.md), [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md) и [отчёт 04.8](reports/04.8-project-script-asset-registry.md).

## D075 — Project-local C# build manifest and immutable outputs (04.9)

- Дата: 2026-11-14.
- Статус: принят для локального Windows prototype; не утверждает .NET 10 или эту схему для production.
- Проект явно задаёт `.astro/csharp-build.json` с schema version, точной версией SDK, target framework и assembly name. Builder собирает только regular `.cs` files ниже `Scripts/`, использует пустой NuGet source list и не меняет исходники.
- Успешный build создаёт новую generation directory под ignored `Library/Scripts/<assembly>/`; предыдущие generation outputs не перезаписываются и автоматически не удаляются. `build-state.json` атомарно связывает source/config fingerprint с путями и hashes DLL/runtimeconfig; `-Check` обнаруживает source/manifest/SDK/output drift.
- Неудачная компиляция не меняет последнее успешное состояние. Build state не встраивает script mapping в AssetRegistry: вызывающий проект явно регистрирует AssetId/type/output. CLI запускает обычный пользовательский C# код с правами пользователя, поэтому не является sandbox и не должен вызываться для недоверенного кода без будущей permission/approval модели.
- Покрытие `runtime.csharp_project_build.integration`: offline build, детерминированные assembly bytes, Unicode path и UTF-8 validation, fresh/stale CLI exit codes, изменение source и immutable generation, сохранение build state при compile failure и output hash tampering. Подробности — [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт 04.9](reports/04.9-csharp-project-build.md).

## D076 — Explicit C# build-to-AssetRegistry handoff (04.10)

- Дата: 2026-11-14.
- Статус: реализован как дополнительный Windows prototype step; не утверждает финальную схему scripting pipeline.
- Проектный manifest опционально перечисляет ровно выбранные пары `sourcePath` / `managedTypeName`. Сборщик генерирует UTF-8 `Library/Scripts/asset-bindings.state` версии 1 и связывает его SHA-256 с `build-state.json` версии 2; source/type соответствие не выводится эвристикой из имён файлов.
- AssetRegistry сообщает `Missing/Current/Stale/Invalid`, проверяя handoff, точные manifest/source sets и content hashes безопасно разрешённых outputs. Явный `registerScriptsFromBuild()` валидирует все binding records до одной атомарной записи index; прежний AssetId сохраняется для того же source. Editor показывает эту диагностику и требует отдельного действия для регистрации.
- Сканирование проекта не запускает compiler, hostfxr или C# код. Managed type существование проверяется лишь при явном host load; успешная registration не означает, что CLR assembly/type успешно загрузится. RuntimeSession lifecycle, code editor, hot reload и sandbox остаются отдельными задачами.
- Обоснование: связывает проверенную project build generation с уже существующей моделью AssetId без автоматического выбора пользовательских типов и без побочных эффектов при открытии проекта.
- Проверки `assets.registry.integration`, `runtime.csharp_host.integration` и `editor.smoke`; детали формата, UX, команды и ограничения: [C# scripting API](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md) и [отчёт 04.10](reports/04.10-csharp-build-registry-diagnostics.md).

## D077 — Одноразовое согласие перед C# Play execution (04.11)

- Дата: 2026-11-14.
- Статус: политика и Editor consent gate реализованы; C# host/lifecycle всё ещё не подключены к Play.
- Editor должен до загрузки managed assembly показать прямому пользователю точный набор включённых `ScriptBehavior` в текущих project/Scene: AssetId, source, managed type, build fingerprint и hashes выходов. Подтверждение распространяется ровно на этот набор и одну Play-сессию.
- Согласие эфемерно и не сохраняется как переиспользуемое доверие в проекте или настройках пользователя. Stop, повторный Play, смена project/Scene, состава, manifest или build/source/output hash до host load сбрасывает grant. После подтверждения весь набор должен быть разрешён повторно и сравнен с показанным snapshot непосредственно перед host load. Cancel или любой missing/stale/invalid binding блокирует lifecycle callbacks для всего набора; частичный запуск не допускается.
- AI, AIEditorApi, CLI и результат сборки не выдают согласие. AI может запросить тестовый Play, но должен ждать прямого ввода пользователя в Editor. Низкоуровневый `CSharpScriptHost::loadScriptAsset` сейчас не содержит gate; будущий Editor обязан проверять grant до этого trusted in-process API.
- Обоснование: код C# загружается в процесс редактора и получает его пользовательские права. Одноразовый диалог предотвращает случайный запуск и делает согласие конкретным, но не ограничивает файлы, сеть, OS API или нативные вызовы и поэтому не является sandbox.
- В 04.12 Editor показывает project/Scene, включённые script bindings и build/output hashes; на Allow делает повторный capture и принимает только идентичный снимок. Approval хранится только в памяти и отзывается при Stop/new Play/project or Scene replacement/Faulted. `editor.csharp_play_approval.unit`, `editor.localization.unit` и `editor.smoke` покрывают модель и startup; UI interaction не автоматизировано. C# host/load/lifecycle не запускаются. Детали: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [AI_INTEGRATION.md](AI_INTEGRATION.md), [04.12 report](reports/04.12-csharp-editor-approval.md).

## D078 — C# ScriptBehavior lifecycle through RuntimeSession (04.13)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows x64 prototype; host остаётся opt-in.
- Runtime загружает все enabled ScriptBehavior до вызова component lifecycle и сохраняет Scene insertion order. Перед каждой уникальной assembly load Editor callback повторно сверяет актуальный project/Scene/script/build snapshot. `OnCreate` выполняется перед Play, `FixedUpdate` на каждом fixed step, `OnDestroy` для созданных компонентов в обратном порядке при Stop/rollback.
- Компонентный ABI теперь требует managed `OnCreate`, `FixedUpdate`, `OnDestroy` entry points; низкоуровневый generic single-entry `CSharpScriptHost::load` оставлен совместимым. Existing users of `loadScriptAsset` must add both lifecycle methods and rebuild/register. Scene serialized schema is unchanged.
- При ошибке `OnCreate` RuntimeSession отклоняет старт и выполняет rollback; Stop всегда очищает runtime Scene, возвращая ошибку через `stopChecked`. Legacy `stop()` сохраняет исходный void/noexcept контракт.
- Обоснование: подключает сериализованный пользовательский binding к фактической Play-сессии, сохраняя one-shot approval и изоляцию authored Scene, без автоматического выбора/запуска скриптов до согласия.
- Ограничения: C# работает с полномочиями Editor process; согласие не является sandbox, module initializer может выполниться во время загрузки, unload не гарантируется; нет managed instance state, crash containment, hot reload, мобильного host или shipping review. Tests: Release full CTest 45/45; Debug without optional host 43/43. Debug C# host не линковался из-за `_ITERATOR_DEBUG_LEVEL` mismatch в локальном `libnethost.lib`.
- Подробнее: [C# scripting API](CSHARP_SCRIPTING_API.md), [AI integration](AI_INTEGRATION.md), [report 04.13](reports/04.13-csharp-runtime-lifecycle.md).

## D079 — Transactional Inspector authoring for ScriptBehavior (04.14)

- Дата: 2026-11-15.
- Status: implemented as a Windows Editor prototype.
- Inspector selects registered script bindings from the current project build; Add creates a disabled component. Enabling requires a current verified build report whose AssetId, managed type and source path still match AssetRegistry. Disabling and removal stay available to recover stale scenes. Play approval remains one-shot and the runtime gate still performs its own complete snapshot check.
- Expose add, set-enabled and remove as typed Scene commands so GUI/AI callers use the same validation, audit and transaction history. Add/remove Undo/Redo restore the exact ComponentId and component-list insertion position; Scene v5 format is unchanged.
- Rationale: support authoring existing compiled bindings without introducing an embedded source editor or silently loading/executing user code.
- Limits: Inspector smoke is programmatic; physical ImGui interaction and a current build dropdown with a live project were not automated. This UI does not edit `.cs` files, build scripts, create registry bindings, provide sandboxing, or support hot reload.
- Tests: `scene.command.integration` verifies validation, lifecycle toggling, stable-ID/order add/remove Undo/Redo; `editor.smoke` verifies Inspector presence and disabled ScriptBehavior Scene v5 save/open. Exact Debug/Release results are in [04.14 report](reports/04.14-scriptbehavior-editor-authoring.md).

## D080 — C# Code workspace baseline (04.15)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows Editor prototype; P15 выполнен частично.
- Workspace редактирует только UTF-8 `.cs` под `Scripts/` проекта, с пределом 4 MiB на source, атомарной публикацией сохранения и явной обработкой unsaved changes при переключении документа/проекта.
- Сборка запускает уже существующий offline C# project builder только по явной кнопке и только в конфигурации Editor с настроенным локальным .NET SDK. Compiler error/warning location переводит Editor к source-файлу и строке.
- Обоснование: дать автору встроенное базовое редактирование без второй IDE/runtime зависимости и переиспользовать зафиксированный build pipeline.
- Ограничения: Windows-only, no syntax highlighting/IntelliSense/LSP/debugger/refactoring; UI-клики не автоматизированы. In-process C# остается недоверенным кодом с правами пользователя; одноразовый Play consent не sandbox. Подробности и тесты: [отчёт 04.15](reports/04.15-csharp-code-workspace.md).

## D081 — Project-owned C# build manifest settings (04.16)

- Дата: 2026-11-15.
- Статус: реализован prototype UI для существующего schema v1 `.astro/csharp-build.json`.
- Настройки принадлежат проекту: SDK pin, `net10.0`, assembly name и список source/type bindings; сохранение выполняется явной кнопкой, build pipeline использует сохранённый манифест.
- Формат строгий: неизвестные/повторные поля, неверные типы, неподдерживаемая версия, traversal и отсутствующие script sources отклоняются. Запись выполняется через временный файл и безопасную замену.
- Обоснование: дать автору настройку существующего C# build pipeline в Editor без второго формата конфигурации.
- Ограничения: Windows Editor prototype; доступны только .NET SDK 10.0.x и framework `net10.0`; другие SDK/framework выбрать нельзя. UI smoke проверяет открытие и сохранение во временном проекте программно, но не автоматизирует мышь/клавиатуру. .NET 10 не утверждён для production. Подробности: [отчёт 04.16](reports/04.16-csharp-build-manifest-editor.md).

## D082 — C# binding diagnostics must not execute project code (04.17)

- Дата: 2026-11-15.
- Статус: принят для текущего prototype.
- Project panel сравнивает явные manifest/build handoff bindings с AssetRegistry и указывает причину расхождения: отсутствующая запись, другое имя managed type, изменённый source hash или иные output paths. Явная регистрация текущей успешной сборки синхронизирует запись, сохраняя AssetId исходника.
- Обычная диагностика/открытие проекта не загружает managed assembly. Реальное наличие CLR type и ABI entry points проверяется только существующим явным Play path после consent; отдельный type probe не вводится без sandbox/permission решения, поскольку загрузка может выполнить module initializer в процессе редактора.
- Обоснование: сделать расхождения реестра понятными, не превращая refresh Project panel в скрытый запуск пользовательского кода.
- Ограничение: диагностика не доказывает наличие CLR type/методов; в текущем стеке нет проверенного статического metadata verifier. C# остаётся доверенным in-process кодом. Подробности: [отчёт 04.17](reports/04.17-csharp-binding-diagnostics.md).

## D083 — C# metadata inspection and execution isolation assessment (04.18)

- Дата: 2026-11-15.
- Статус: принято как направление проектирования; реализация и выбор/фиксация parser dependency не входят в этот подэтап.
- Не загружать пользовательские C# assemblies в Editor для Project diagnostics. Будущий verifier должен работать отдельным процессом и возвращать ограниченный структурированный результат. Предпочтителен metadata-only inspection без вызова пользовательского кода; конкретную библиотеку и версию необходимо отдельно проверить и закрепить. `MetadataLoadContext` предотвращает выполнение кода при metadata inspection, но не является sandbox; `MetadataReader` нельзя считать безопасным для произвольного недоверенного PE.
- Сам verifier должен быть изолирован средствами ОС: Windows AppContainer с минимально необходимыми read-only capabilities и без сети/профиля/секретов; ограниченные временные каталоги; строгие лимиты входа и времени. Windows Job Object рекомендуется для лимитов ресурсов и остановки всего дерева процессов, но не считается границей доступа к файлам/сети.
- Долгосрочный C# runtime для недоверенных проектных скриптов должен работать вне Editor в отдельном ограниченном процессе через узкий версионируемый IPC. Действующий ABI v1 и in-process host остаются совместимыми и предназначены только для явно доверенного локального prototype. Миграция на новый ABI требует отдельного opt-in плана; автоматической миграции нет.
- Consent gate не превращает скрипт в sandbox. AI/Codex может запросить запуск, но не может выдать пользовательское согласие или предоставить worker дополнительные capabilities.
- Будущая приёмка включает отсутствие исполнения module initializer при инспекции, устойчивость Editor к malformed/oversized input, проверку AppContainer deny/allow policy, лимиты/kill worker и совместимость текущего host. См. [оценку 04.18](reports/04.18-csharp-metadata-isolation-assessment.md).

## D084 — Windows C# metadata-validator worker prototype (04.19)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows x64 прототип; не подключён к Editor и не является обещанием безопасного запуска недоверенного C#.
- Для чтения CLR metadata использовать `System.Reflection.Metadata` из целевого .NET 10 shared framework без отдельной NuGet-зависимости. Проверка принимает один PE до 64 MiB, ищет явный managed type и сверяет три ABI v1 entry points и `UnmanagedCallersOnlyAttribute` без загрузки assembly и создания атрибутов.
- Парсер запускается в отдельном Windows AppContainer worker без сетевых capabilities. На один запрос создаётся временная рабочая область с копией runtime/worker и ровно выбранной DLL; разрешения AppContainer выдаются только на эту область. Временный профиль удаляется после запуска. Environment block worker ограничен allowlist системных путей и .NET переменных, а не наследуется от Editor.
- Worker ограничен 16 KiB request, 32 KiB response, 15 s operation timeout и 512 MiB process-memory Job Object; timeout probe доказывает остановку worker. Запуск Windows x64 и AppContainer deny file/network проверяются integration-тестом. `MetadataReader` по документации Microsoft не предназначен для недоверенного input, поэтому защита всё ещё зависит от Windows isolation и не доказывает отсутствие иных OS/runtime attack surfaces.
- Никакой Project panel/AI API не вызывает worker в 04.19. C# in-process host ABI v1 не менялся и остаётся только для доверенных локальных прототипов; миграции нет.
- Подробности, команды и фактические ограничения: [отчёт 04.19](reports/04.19-csharp-metadata-validator-worker.md).

## D085 — Project panel C# metadata diagnostics (04.20)

- Дата: 2026-11-15.
- Статус: реализовано как явная Windows x64 диагностика; C# Play host ABI v1 не меняется.
- Проверка выполняется только по кнопке, только для свежей сборки и текущих зарегистрированных AssetRegistry bindings. Editor не загружает пользовательскую DLL: запускается существующий metadata worker вне процесса Editor; результат читается асинхронно.
- Native runner принимает только нормализованный относительный `Library/Scripts/*.dll`, проверяет reparse points и канонический путь под project root, передаёт только текущие registered managed types. C# сборка недоступна одновременно с worker, чтобы файлы output не менялись во время staging.
- Worker API принимает максимум 16 типов за одну изолированную сессию; для больших проектов Editor разбивает список на пакеты. Копирование runtime для каждого пакета может быть медленным. Совокупный вывод ограничен 32 KiB, усечение указывается.
- Ограничения: Windows-only; metadata parser не сертифицирован для враждебных PE; AppContainer проверен на текущем хосте, но не является доказательством отсутствия иных атак; Play host остаётся trusted-code-only in-process.
- Тесты, команды и результаты: [отчёт 04.20](reports/04.20-csharp-project-metadata-diagnostics.md).

## D086 — C# metadata validation batch limits (04.21)

- Дата: 2026-11-15.
- Статус: реализовано для Windows x64; runtime ABI, sandbox, AppContainer ACL и runtime staging не менялись.
- Один worker может проверять до 64 registered managed types при общем UTF-8 лимите имён 8 KiB; протокольные request/response остаются в пределах 16/32 KiB, type count проекта ограничен 256. Editor делит план по обоим лимитам.
- Общий Editor report cap установлен в 128 KiB, worker response cap — по-прежнему 32 KiB. При потере содержимого вывод содержит явную пометку. Панель показывает число запрошенных типов и isolated batches.
- На локальном Windows Release synthetic ABI fixture один baseline-прогон четырёх последовательных 16-type сессий занял 5.48 s; после изменения медиана трёх 64-type прогонов составила 1.35 s. Типовая форма соответствует C# ABI, но не включает зависимости/объём реального пользовательского проекта; измерение не является общим benchmark для других устройств.
- Отдельный shared runtime cache не добавлен: измеренная batching правка сокращает типичный overhead без постоянных файлов или расширения ACL/AppContainer доступа. Реальный проект следует профилировать отдельно перед cache-дизайном.
- Подробности, тесты и логи: [отчёт 04.21](reports/04.21-csharp-metadata-batch-performance.md).

## D087 — Профиль C# metadata и UX большого отчёта (04.22)

- Дата: 2026-11-15.
- Доступная в репозитории сборка `AstroEngine.CSharpSample` содержит только один managed type. Её пять Release замеров (медиана 1,456 с, ответ 282 байта) полезны как baseline этой машины, но не обосновывают latency для большого игрового проекта.
- На машине найден отдельный Unity 6.5.2 проект; его cached `Assembly-CSharp.dll` stale для одного исходника и его `MonoBehaviour` не соответствует текущему AstroEngine host ABI. Validator прогоны отклонены ожидаемо; считать их успешной profile/compatibility проверкой нельзя. Не запускать Unity batch build в рамках этого этапа, поскольку он меняет generated Library project state. Абсолютный локальный путь не записывать в репозиторные docs.
- Smoke создаёт только в `--smoke` синтетический текстовый отчёт 128 KiB; тест фиксирует прокрутку и время отрисовки текста (<250 мс). Это проверка производительности виджета, не визуальное UX-исследование с реальными диагностическими строками.
- Не менять лимиты batch/report, не добавлять долгоживущий staging/runtime cache, не ослаблять AppContainer и не менять ABI по результатам 04.22: найденная Unity DLL мала, устарела для одного script и оба requested bindings отклонены. Это подтверждает лишь время обработки неподдерживаемого input. Текущий AstroEngine C# host не обещает Unity MonoBehaviour compatibility; такой adapter потребует отдельного product/API/security design и явного подэтапа.
- Полный CTest на текущем Windows x64: Debug 46/46, Release+C# 49/49. Детали: [отчёт 04.22](reports/04.22-csharp-metadata-project-ux.md).

## D088 — Runtime Game UI host content for Puzzle demo (05.7)

- Дата: 2026-11-15.
- `ImGuiGameUiAdapter` exposes an optional host-owned gameplay renderer callback. The Game UI model remains backend-neutral; the SDL demo owns `PuzzleGame` and draws its board in the Gameplay body.
- Keep the demo board fixed at a deterministic 8×8 seed for reproducible smoke checks. Reuse `PuzzleGame::swap`, the existing HUD model, and RuntimeSession transitions; do not duplicate puzzle rules in UI code.
- Build `astro_runtime_puzzle` when the optional Game UI demo is enabled, even with `BUILD_TESTING=OFF`, so packaged runtime demo has its gameplay dependency without making Puzzle mandatory for the headless engine.
- This is an executable sample integration only. It does not satisfy multi-level campaign, persistent progression, physical-input automation, board visual golden, or clean packaged ZIP acceptance. No current released ZIP was replaced.
- Results and limitations: [05.7 report](reports/05.7-puzzle-game-ui.md).

## D089 — Standalone runtime host for authored Puzzle campaigns (05.8)

- Date: 2026-11-15.
- The optional SDL Game UI demo accepts an explicit project root and campaign ID and loads through `PuzzleCampaignSession`; the existing campaign parser/resolver/session remain the single source of campaign behavior.
- Apply submitted tile swaps through `RuntimeSession` fixed-step callbacks. A reached score target enables one explicit Continue action. Each action resolves one transition and loads the selected level; terminal `END` retains the final level and score until the user returns to the menu.
- Preserve the no-argument seeded Puzzle demo and headless/default optional-module policy. Windows command-line paths are converted from wide arguments to UTF-8 and then to `std::filesystem::path`.
- Add a small repository sample project and read-only end-to-end smoke. Campaign progress saves, auto-advance, external project packaging and physical input automation remain outside this scope.
- Results and limitations: [05.8 report](reports/05.8-runtime-puzzle-campaign.md).

## D090 — Explicit campaign progress slots in standalone Game UI (05.9)

- Date: 2026-11-15.
- Reuse SaveStore's versioned slot format and integrity checks for authored Puzzle campaign progress; do not introduce a parallel campaign file format.
- Persist only explicit custom fields: campaign ID, active level ID, terminal END flag, and deterministic Puzzle snapshot. The sample selects no Scene properties and performs no automatic writes.
- Store slots in SDL user data under a stable FNV-1a key derived from the canonical project path, followed by campaign ID. Keep player progress outside project content.
- Validate profile/game version, campaign identity, listed level ID, snapshot compatibility with current authored rules, and END resolution before publishing the restored candidate Scene/session. Failed loads are non-mutating.
- Scope is the standalone SDL Game UI only. Editor Game View slots, migrations, autosave, and slot previews remain future work.
- Results and limitations: [05.9 report](reports/05.9-campaign-progress-save.md).

## D091 — Refresh the Windows runtime package after 05.9

- Date: 2026-11-15.
- Rebuild the existing opt-in CPack ZIP from current sources after adding Puzzle/campaign gameplay and manual campaign progress. Keep the established package contents (standalone demo, runtime README, SDL3 and Dear ImGui license texts); do not add authored project files, Editor, or build tools.
- Generate into a new build directory so the earlier `build/05.6-runtime-release` archive remains untouched.
- Validate both the registered package smoke (fresh extraction and `--smoke`) and direct smoke from the exact generated archive, including the sample campaign's save/restore path.
- This reissues a runtime demo package; it is not full-game export or Stage 05 completion. See [package refresh report](reports/05.6-package-refresh-after-05.9.md).

## D092 — Bundle the sample campaign with the Windows runtime package (05.10)

- Date: 2026-11-15.
- Include only the checked-in two-level Puzzle campaign in the opt-in demo ZIP, under the relative ASCII path `game-data/puzzle-campaign`; preserve the source sample unchanged.
- Include `launch_puzzle_campaign.cmd`; resolve the executable and project root through `%~dp0` so launch behavior does not depend on the caller's working directory.
- Keep player progress in SDL user data outside the archive. Do not imply general project export, Editor packaging, or completed Stage 05 acceptance.
- Automated package smoke invokes the executable directly with bundled data; separately verify the `.cmd` launcher manually because CMake `execute_process`/`cmd.exe` quoting was not reliable in this environment.
- Results and limitations: [05.10 report](reports/05.10-self-contained-campaign-package.md).

## D093 — Editor Game View campaign progress slots (06.31)

- Date: 2026-11-15.
- Reuse `PuzzleCampaignSaveAdapter` and the existing versioned SaveStore format for Editor Game View; do not create a second campaign save format.
- Opt in only to campaign ID, active level ID, terminal END, and Puzzle snapshot. The profile selects no Scene properties or unrelated game state.
- Keep player data outside project content under the configured SDL user-data subdirectory, then `PuzzleCampaigns/<campaign-id>`. Offer ten explicit manual slots (0–9).
- Load through the existing validated candidate path and replace the active campaign session only after all slot and authored-level checks pass.
- Autosave, slot previews, migrations, and arbitrary game-state persistence remain out of scope.
- Results and limitations: [06.31 report](reports/06.31-editor-campaign-progress.md).

## D094 — Give Editor Game View campaign controls usable first-use space (06.32)

- Date: 2026-11-15.
- Give the ordinary interactive Game View a 520×560 first-use window so the campaign board and explicit save controls have usable initial space.
- Keep slot selection on its own row and place Save/Load actions on the following row; do not require horizontal scrolling for primary actions.
- Do not apply the interactive window-size override in screenshot smoke or benchmark runs, preserving their established graphics capture geometry.
- This is a layout decision only; SaveStore format, campaign data and slot policy remain those of D093. Results: [06.32 report](reports/06.32-editor-campaign-save-ui.md).

## D095 — Keep campaign save namespaces stable and never migrate implicitly (06.33)

- Date: 2026-11-15.
- Resolve the Editor path as SDL user-data root + the relative `userDataSubdirectory` stored in `.astro/puzzle-save.settings` + `PuzzleCampaigns/<campaign-id>`.
- A campaign ID change selects another directory; leave the previous slots untouched. Do not rewrite saved campaign identity or silently copy slots.
- A project move or copy that carries the settings sidecar keeps its relative save namespace. Copies therefore share slots until the user explicitly configures another subdirectory. Projects without a settings sidecar continue to derive a default subdirectory from the current canonical project path.
- Do not implement automatic migration. A future copy/move UI must preview source/target, preserve source data, reject collisions, and require explicit approval before copying or replacing files.
- The resolver validates the relative directory and campaign ID but performs no directory creation or file I/O. Tests use only the Editor smoke's temporary fixture. See [06.33 report](reports/06.33-campaign-save-identity.md).

## D096 — Do not copy campaign saves across campaign identities (06.34)

- Date: 2026-11-15.
- The current progress payload stores `puzzle.campaign.id`; `loadPuzzleCampaignProgress` rejects it when it differs from the requested campaign ID. A file-only copy cannot migrate a renamed campaign safely.
- Any future initial transfer support is limited to the same campaign ID and must begin with a read-only preview of slots 0–9, validation/recovery status, destination collisions and total bytes. The preview itself must not create directories or alter files.
- A later write operation must be copy-only, preserve every source artifact, refuse destination collisions by default, recheck the preview immediately before writing, and use the existing atomic-save guarantees. Payload identity/schema transformation requires a distinct migration design and tests.
- No transfer or save migration was implemented in 06.34. User-data writes require separate explicit confirmation after reviewing a concrete preview. See [06.34 assessment](reports/06.34-campaign-save-transfer-assessment.md).

## D097 — Keep the first campaign transfer preview read-only and same-ID (06.35)

- Date: 2026-11-15.
- Add a runtime API that inspects only slots 0–9 and requires the expected campaign ID. It reports empty, valid, backup-recovered, invalid and mismatched slots, expected-file byte counts, primary/backup destination collisions, and unexpected source artifact names.
- Preview must not create directories, copy files, or rewrite payloads. Use `SaveStore::inspectExistingSlot` to inspect an existing source slot without exposing a writable store, including the validated primary or recovered backup.
- Reject parent traversal and symlink/reparse points in source/target directory paths and expected slot paths.
- This preview checks save-format integrity and the embedded identity/required-field types. It does not validate a saved Puzzle snapshot against current authored campaign levels; the normal load path remains the compatibility check.
- File transfer, repair of corrupt primaries, GUI presentation and any user-data writes remain separate work. Results and limits: [06.35 report](reports/06.35-campaign-save-transfer-preview.md).

## D098 — Show campaign save transfer preview in Editor, read-only (06.36)

- Date: 2026-11-15.
- `Puzzle Save Settings` may call the 06.35 same-ID preview using the applied source settings and destination directory currently in the edit field. The preview is cleared whenever that destination changes.
- The preview must not persist settings, create a target directory, copy/repair/delete files, or alter save payloads. Resolving the Windows app data root reads `%APPDATA%`; it must not call APIs that implicitly create the application data directory.
- Smoke uses a temporary fixture and asserts the UI preview path is reached, the absent destination remains absent, and source slot bytes are unchanged.
- Show per-slot format status, recovery state, bytes, collision status and unexpected artifact names. Warn that this does not validate Puzzle snapshot compatibility against current authored campaign rules.
- A future transfer action requires its own substage and concrete preview revalidation before writes. See [06.36 report](reports/06.36-campaign-save-transfer-editor-preview.md).

## D099 — Revalidate campaign save transfer preview before write (06.37)

- Date: 2026-11-15.
- A future copy action must rerun inspection immediately before writing and must not use a displayed preview as authority by itself. The revalidation API added in 06.37 performs no writes and reports `Ready`, `StaleSource`, `StaleDestination`, `DestinationConflict`, or `SourceNotTransferable`.
- Source freshness compares slot state, identity, exact byte totals, primary/backup presence and bounded content fingerprints. FNV-1a detects ordinary stale changes; it is not a cryptographic integrity check or security boundary.
- Any expected destination primary or `.bak` path with an existing filesystem entry is a conflict, including zero-byte files and directories. The default policy is refusal; overwrite and merge behavior are not specified.
- Invalid, mismatched, oversized or incompletely fingerprinted source slots and unexpected source artifacts block `Ready`. Empty numbered slots are allowed, but there must be at least one source slot to copy.
- Any future writer is copy-only, preserves source bytes, revalidates immediately before execution, uses exclusive target creation, and requires explicit user confirmation for that operation. Atomic/partial-failure cleanup semantics must be designed and tested before implementation.
- No file transfer, settings mutation, target directory creation or migration was added in 06.37. See [06.37 report](reports/06.37-campaign-save-transfer-preflight.md).

## D100 — Same-ID copy-only campaign save transaction (06.38)

- Date: 2026-11-15.
- Runtime may copy only recognized primary and `.bak` files from a `Ready` 06.37 preflight. Copy bytes verbatim; do not rewrite payload identity/schema. Invalid, mismatched, oversized, unclassified or unexpected source artifacts remain ineligible.
- Require a caller-supplied explicit-confirmation flag for every invocation that can write. This flag is not an Editor approval system; the 06.38 API is not connected to a user-facing button.
- Create each target file exclusively (`CREATE_NEW` on Windows, `O_EXCL` on POSIX). Never replace or merge an existing target path. Revalidate before creating target directories, then rely on exclusive file creation to reject races during the batch.
- On cancellation or ordinary I/O failure, remove only exact created outputs that still match this attempt and remove only empty directories created by this attempt. Preserve competing/pre-existing files. Report remaining artifacts if rollback is incomplete.
- This is rollback on detected failure, not crash-atomic multi-file commit. A process/power failure may leave partial target files; a future UI must disclose that and offer diagnostics. Source files remain untouched.
- No real user save directory was used. Unit/integration coverage uses isolated temporary folders. See [06.38 report](reports/06.38-campaign-save-copy-transaction.md).

## D101 — Editor confirmation before campaign save copy (06.39)

- Date: 2026-11-15.
- A preview alone never copies data. The Editor requires a separate confirmation dialog for each operation and shows canonical source/target paths, eligible slot count, byte total and no-overwrite policy before the confirm action.
- The confirm action invokes the 06.38 copy API, which revalidates immediately before writes. Editor reports copy state/count/diagnostic and every remaining artifact when rollback is incomplete.
- The Editor smoke invokes that same confirmation action only against a unique temporary fixture and verifies the source remains and the copied slot bytes match. It does not exercise real AppData saves.
- Same-ID copy only; no payload migration, merge or overwrite. Process/power interruption may still leave partial files, as the transaction has no crash-recovery journal. See [06.39 report](reports/06.39-campaign-save-transfer-editor-confirmation.md).

## D102 — Journal and report campaign save transfer interruption (06.40)

- Date: 2026-11-15.
- Before copying any slot artifacts, create an exclusive bounded `.astro-campaign-transfer.pending` journal containing the safe campaign ID and the planned slot/kind/size/fingerprint list. Flush it before the first output. Normal success and handled rollback remove only the exact journal bytes created by that operation.
- A later preview reports the journal and classifies each expected target artifact as `Complete`, `Partial`, `Missing`, `Changed`, or `Unsafe`. Any pending journal blocks another transfer. Invalid journals are reported and cannot be acknowledged by the Editor API.
- Editor acknowledgement requires a separate confirmation and removes only an unchanged valid journal after re-inspecting the listed artifacts. It never deletes, repairs or overwrites campaign save files. Partial/changed files require manual review; after acknowledgement, ordinary destination conflict checks still protect them from overwrite.
- Integration coverage terminates a child test process after its first copied artifact, then inspects the state from the parent process using Unicode temporary paths. The test is a process-interruption test; it does not simulate power loss or guarantee directory-entry durability on all filesystems. No real project/user data is used. See [06.40 report](reports/06.40-campaign-save-transfer-recovery-journal.md).

## D103 — Explicit resume for campaign save transfers (06.41)

- Date: 2026-11-15.
- A valid interrupted transfer may be resumed only through a separate explicit-confirmation API/UI action. Existing copy behavior remains unchanged and stays blocked when a recovery journal exists.
- Immediately before resume, re-read and compare journal identity/fingerprint/artifact states, compare the displayed source preview with fresh source inventory, and require the journal's slot/kind/size/fingerprint inventory to match the current source. Source location itself is not embedded in journal v1; this is content/inventory matching, not provenance authentication.
- Skip only exact `Complete` target artifacts. Create only `Missing` artifacts with exclusive creation. `Partial`, `Changed`, `Unsafe`, changed-after-review artifacts, changed sources, and racing destination files block/abort. Never repair or overwrite a target artifact.
- On detected error/cancellation, rollback only exact outputs created by this resume invocation and retain the journal. Remove the unchanged journal only after all listed artifacts inspect as complete. Resume does not provide cross-process locking, power-loss durability, automatic cleanup, or cryptographic integrity.
- Integration coverage uses isolated Unicode temporary fixtures and child-process interruption; Editor smoke checks the recovery UI offers the resume control. No real project saves are touched. See [06.41 report](reports/06.41-campaign-save-transfer-resume.md).

## D104 — Preserve partial campaign save targets (06.42)

- Date: 2026-11-15.
- A target artifact classified `Partial` remains a conflict. The resume API must not delete, truncate, append to, repair, or overwrite it; it returns `DestinationConflict` and retains the recovery journal.
- The Editor must identify partial artifact paths/status and disable resume while any partial artifact exists. The user must inspect it manually before taking any separate cleanup action.
- Journal v1 stores only the complete artifact byte count/fingerprint; it does not bind a partial file's observed prefix to the transfer attempt. Matching source-prefix bytes are not proof of provenance. Therefore no automatic partial recovery is considered safe, and no journal format migration is introduced here.
- Integration coverage constructs a shorter target file after child-process interruption and verifies the resume refusal leaves its exact bytes, the missing companion file and the journal unchanged. Editor smoke checks a partial fixture displays the warning and non-resumable state. No real save path is used. See [06.42 report](reports/06.42-partial-campaign-save-transfer-policy.md).

## D105 — Diagnose invalid campaign transfer journals (06.43)

- Date: 2026-11-15.
- Recovery inspection reports a typed journal state: `Unreadable`, `Unsafe`, `Oversized` or `Malformed`. Successfully parsed records use `Valid`; absent records use `Missing`.
- Invalid journal data never supplies inferred target artifact paths. Copy and resume stay blocked by recovery-required state; acknowledgement refuses invalid records. Editor displays the category and parser/read diagnostic and instructs manual inspection without changing save files.
- Tests cover an unsupported version, truncated v1 body, over-limit journal, and a directory occupying the journal path. They assert no candidate slot path is inferred and preserve the journal/path occupant and a slot-looking file. No real project data is used. See [06.43 report](reports/06.43-invalid-campaign-transfer-journal-diagnostics.md).

## D106 — Archive readable malformed campaign journals (06.44)

- Date: 2026-11-15.
- Only a bounded, readable record classified `Malformed` is eligible for the in-Editor archive operation. Require a separate explicit confirmation.
- Copy the exact reviewed bytes to a deterministic adjacent archive path using exclusive creation. If that path exists, accept it only when it is a regular readable file with identical bytes; never replace a different occupant.
- Historical 06.44 behavior attempted to retire the source after a verified archive. The 06.45 review supersedes that part; see D107. The current flow never removes the malformed source journal.
- Slot artifacts are never inferred, opened, or changed. `Unreadable`, `Unsafe` and `Oversized` journals remain manual-review cases. No journal format migration is added. See [06.44 report](reports/06.44-malformed-campaign-journal-archive.md) and [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md).

## D107 — Preserve malformed journals during archive (06.45)

- Date: 2026-11-15.
- A final path reread followed by `std::filesystem::remove(path)` is a TOCTOU window: another process may replace the path between validation and deletion. Portable C++ filesystem APIs do not provide an atomic compare-and-delete operation.
- The confirmed action now exclusively creates an adjacent exact-byte archive copy, verifies it, and leaves the original malformed journal in place. The journal continues to block transfer/resume until a person reviews it. Identical archive retries are idempotent; different existing contents are preserved and cause failure.
- At 06.45 the malformed archive flow had no cross-platform lifecycle lock or multiprocess race coverage. 06.48 adds a tested source-journal lease but still does not retire or delete the original malformed journal. See [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md) and [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## D108 — Handle-based valid recovery journal acknowledgement (06.46)

- Date: 2026-11-15.
- On Windows, acknowledgement opens the reviewed journal with `GENERIC_READ | DELETE`, denies new write/delete sharing, reads and compares the bounded exact bytes through that same handle, then marks that handle for deletion with `SetFileInformationByHandle(FileDispositionInfo)`. If the file is busy, replaced, reparse-backed, changed or the filesystem refuses disposition, the API fails and leaves the journal in place. It never falls back to `std::filesystem::remove(path)`.
- The caller must still provide explicit confirmation and the current artifact list must match the displayed recovery review. Save artifacts are not removed, rewritten or made overwriteable by acknowledgement.
- At the end of 06.46 POSIX acknowledgement still failed closed; that platform gate was satisfied in 06.47 and the current implementation is documented in D110. The original requirement was `O_NOFOLLOW`, advisory `flock`, open-inode comparison with `fstatat(..., AT_SYMLINK_NOFOLLOW)` on a held directory descriptor, and `unlinkat`. Advisory locks coordinate only cooperating processes; they do not defend against arbitrary local writers that ignore the lock.
- Windows integration tests use isolated Unicode temp directories and verify stale-preview refusal, refusal while a competing writer handle is open, successful handle-based acknowledgement, and save-artifact preservation. These tests do not claim protection from privileged kernel/filesystem actors or power loss. See [06.46 report](reports/06.46-handle-based-campaign-journal-acknowledgement.md).

## D109 — Gate POSIX journal acknowledgement on a runnable target (06.47 assessment)

- Date: 2026-11-15.
- Historical gate: do not enable POSIX acknowledgement before a runnable POSIX build and integration/race tests exist.
- This gate was satisfied during 06.47 after user authorization to restart Windows, install Ubuntu 24.04/WSL2 and install local build tools in that distribution. See [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md).

## D110 — Cross-platform campaign journal lifecycle lock (06.47)

- Date: 2026-11-16.
- Copy holds the newly created journal lease until commit/rollback; resume holds the existing journal lease through its final artifact verification; acknowledgement validates the displayed preview, takes the lease, rereads exact bytes and removes only that opened file object/verified directory entry. This prevents cooperating transfer/acknowledgement operations from retiring a journal that marks an in-flight transaction.
- Windows uses an exclusive `LockFileEx` byte-range lock and `SetFileInformationByHandle` for removal. The lock permits ordinary open/read handles where Windows share semantics allow them; operations that conflict with a locked byte range fail closed. POSIX uses `openat` with `O_NOFOLLOW`, `flock(LOCK_EX | LOCK_NB)`, `fstatat(AT_SYMLINK_NOFOLLOW)` identity comparison, and `unlinkat` relative to an open directory descriptor.
- POSIX `flock` is advisory: it coordinates only cooperating processes. The directory-entry identity check narrows replacement races but cannot make `fstatat` plus `unlinkat` atomic against hostile non-cooperating local processes. These functions are not a security boundary against privileged/local attackers, nor a power-loss durability guarantee.
- Isolated integration tests cover an active copy, acknowledgement contention from another process on POSIX and a competing range lock on Windows, successful acknowledgement, stale bytes and preservation of slot artifacts. Full Release CTest passed 39/39 on WSL Ubuntu 24.04 and 52/52 on Windows. See [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md).

## D111 — Lock malformed journal archival (06.48)

- Date: 2026-11-16.
- Keep the existing explicit confirmation, bounded `Malformed` eligibility, deterministic archive path, exact-byte verification and no-overwrite behavior. Retain the source malformed journal; 06.48 does not add automatic repair, retirement or deletion.
- Perform read-only path classification before taking the lease, because Windows `LockFileEx` locks the journal byte range against second-handle reads. Then acquire the same lifecycle lease used by copy/resume/acknowledgement, compare the bounded exact bytes through the held source handle, create or verify the archive, and reread the source through that held handle before success.
- If another cooperating operation owns the lease, fail before archive creation and preserve source/archive/save artifacts. POSIX `flock` remains advisory; it cannot prevent a non-cooperating process from changing or replacing the journal. No save slot files are accessed or changed.
- Integration tests verify contention by a separate POSIX process and a Windows range lock, no archive creation while locked, source/save preservation, and normal/repeated archive success after lock release. Full Release CTest: POSIX 39/39; Windows 52/52. See [06.48 report](reports/06.48-malformed-journal-archive-lock.md).

## D112 — Preserve partial journal archive residue (06.49 assessment, superseded by D113)

- Date: 2026-11-16.
- Current archive creation writes directly to its final fingerprint-derived path using exclusive creation. If the process exits after creating that path but before all bytes are written, the partial file remains. A retry sees an existing non-identical archive and fails closed; it does not overwrite or remove it. The malformed source journal remains the recovery source and continues blocking save transfer operations.
- Do not auto-delete or resume a partial archive: the application cannot prove that a mismatching occupant is its own interrupted output rather than user data. Manual inspection is required. The integration test models this state with a truncated file in an isolated temp fixture; it is not a real power-loss/process-termination test.
- The implementation candidate was completed in 06.50; D113 defines current behavior. The historical 06.49 fixture remains useful for verifying fail-closed handling of a pre-existing partial final archive. See [06.49 assessment](reports/06.49-partial-journal-archive-assessment.md).

## D113 — Staged atomic no-replace journal archive publication (06.50)

- Date: 2026-11-16.
- Keep the explicit confirmation, source journal lease, exact reviewed bytes, deterministic final archive name and no-overwrite rule. Write the archive to a unique same-directory staging file, flush it, then verify the exact bytes through the held file handle before publication.
- Windows publishes the open file with `SetFileInformationByHandle(FileRenameInfo)` and `ReplaceIfExists=FALSE`. POSIX verifies the staging directory entry still names the held inode, publishes with same-directory `linkat` (which fails if the final path exists), verifies the final inode, attempts file/directory `fsync`, then unlinks only the verified staging entry. No overwrite-capable fallback is permitted.
- If publication fails before commit, remove the staging entry only through its held lease and only if exact contents and object identity still match. If the process terminates, its staging entry may remain; future attempts use unique names and do not auto-delete unidentified remnants. A pre-existing partial or conflicting final archive remains fail-closed and requires manual review. The original malformed journal and save slots remain untouched.
- Integration tests cover stale staging preservation/retry, exact archive bytes, existing partial-final refusal, collisions, source lease contention and save/source preservation on Windows and POSIX. Tests do not simulate kernel crash or power loss. Windows directory-entry durability is delegated to the filesystem; POSIX directory `fsync` is attempted, but filesystem guarantees vary. See [06.50 report](reports/06.50-staged-journal-archive.md).

## D114 — Subprocess interruption tests for staged journal archive (06.51)

- Date: 2026-11-16.
- Exercise process termination at three deterministic points: after a partial staging write, after exact-byte verification and flush but before publish, and after publish but before staging cleanup. A child process performs the real archive operation; the parent terminates it after a private callback signals the selected boundary.
- Keep the hook in a private runtime test header and invoke it only through an internal test entry point. The shipped archive API accepts no hook parameter and reads no environment variable. Test pauses are controlled by the isolated integration harness and bounded by a timeout.
- Expected states: before publication, final archive is absent and partial/complete staging residue remains; retry publishes the exact final copy without changing old residue. After publication, final bytes are exact and retry is idempotent; POSIX may retain the staging hard link while Windows has renamed it. In every case the malformed journal and candidate slot bytes remain unchanged.
- The test uses `TerminateProcess` on Windows and `SIGKILL` on POSIX. It proves process-interruption behavior on the tested filesystems, not kernel crash, power-loss durability, controller write-cache behavior, or arbitrary hostile concurrent path replacement. Full Release CTest: Windows 40/40, WSL Ubuntu 39/39. See [06.51 report](reports/06.51-archive-interruption-tests.md).

## D115 — Manual review of legacy conflicting archive files (06.52 assessment)

- Date: 2026-11-16.
- Reuse the deterministic archive path only when it is a readable regular file containing the exact reviewed journal bytes. Keep all differing, unreadable, unsafe, directory and reparse-point occupants fail-closed and untouched.
- Do not infer that a short file or byte-prefix match is an interrupted archive. Such a file could be unrelated user data. The archive name contains a 64-bit non-cryptographic change fingerprint; it is a lookup key, not proof of authorship or uniqueness.
- A future diagnostics UI may report the expected path, no-follow file kind, size, readability and bounded byte-comparison category (exact, different, prefix match, unavailable). Any prefix match must be described only as observed bytes, not as verified archive provenance. Keep the malformed source journal and save slots unchanged.
- Recovery for an unknown occupant remains a manual operator task outside the engine. Do not add automatic delete, move, truncate, overwrite or cleanup. No code/API/UI behavior changed in 06.52. Baseline Release CTest passed Windows 40/40 and WSL Ubuntu 39/39. See [06.52 assessment](reports/06.52-legacy-archive-conflict-assessment.md).

## D116 — Read-only diagnostics for archive conflicts (06.53)

- Add a runtime API that inspects only the deterministic archive path associated with a displayed, revalidated malformed journal. Preserve existing archive naming and write behavior.
- Open without following a Windows reparse point or POSIX symbolic link. Classify missing, regular file, directory, link/reparse point, other and unavailable. Read at most the existing 4096-byte journal bound and compare bytes as exact, different, prefix, oversized or unavailable. Recheck the held object's identity/metadata before returning a content comparison.
- Surface the expected path, object type, observed size, comparison, and platform diagnostic in Editor after archive refusal. A prefix describes bytes only and does not establish authorship or prove an interrupted AstroEngine write.
- Inspection never creates, deletes, moves, truncates, replaces or repairs an archive, source journal or save slot. No format/API break or external dependency is introduced.
- Windows Release full CTest: 44/44 (including Editor smoke); WSL Ubuntu Release: 39/39. POSIX link creation is permission-dependent in tests; it passed on the current WSL filesystem. Kernel/power-loss behavior and arbitrary concurrent hostile path replacement are not claimed. See [06.53 report](reports/06.53-archive-conflict-diagnostics.md).

## D117 — Bounded explicit copy of unknown archive occupants (06.54 assessment)

- A future UI may offer a separate, explicit **copy-aside** action only for a readable regular file at the reviewed deterministic archive path, bounded by the existing 4096-byte limit. This is a proposal; 06.54 adds no runtime behavior.
- The operation must no-follow the final source object, retain/revalidate the same source handle while reading exact bytes, and publish a new adjacent copy under a generated distinct name with atomic no-replace semantics. If the source changes or the destination collides, abort and preserve both.
- Require explicit confirmation showing source path, destination path, byte count and a statement that the occupant's creator is unknown. Do not display or infer sensitive file contents. The copied file is an unverified evidence copy, not a validated journal/archive.
- Keep the original conflict and source recovery journal untouched; successful copy-aside does not clear the deterministic path or trigger a retry. Links/reparse points, directories, other object types, unreadable and oversized files are ineligible. Resolution remains manual.
- If implemented later, test cancel/no-write, exact bytes, source/slot preservation, changed/replaced source, destination collision, symbolic link/reparse point, size boundary, Unicode path, and child-process interruption before/after atomic publication on Windows and POSIX. No additional dependency is justified.
- 06.54 was a documentation-only assessment. Pre-edit Release CTest passed Windows 44/44 and WSL Ubuntu 39/39. See [06.54 report](reports/06.54-conflict-copy-assessment.md).

## D118 — Explicit bounded copy-aside for archive conflicts (06.55)

- Date: 2026-11-16.
- Offer copy-aside only after read-only diagnostics identify a differing readable regular file at the deterministic archive path. Limit source size to 4096 bytes; reject links/reparse points, directories, unsupported kinds, unavailable and oversized objects.
- Preview captures the exact bytes and object identity without writing. The Editor confirmation shows exact source/destination paths, byte count and states that author/provenance is unknown and the result is unverified. Cancel does not write.
- On confirmation, reacquire and hold the source no-follow lease, verify object identity and exact bytes against preview, revalidate the malformed recovery journal, and create a unique adjacent staging file. Publish to the reviewed generated adjacent name atomically without replacement. Any stale source or destination race fails closed.
- Preserve the original conflict, malformed journal and candidate save slots. The copied bytes are not parsed as a valid archive, do not clear the conflict and do not start a retry. Staging or published output left by process interruption remains for manual review.
- No additional dependency or format/API migration is introduced. Tests cover Windows and POSIX process-interruption boundaries, not kernel crash/power loss, arbitrary hostile local writers or all filesystem durability semantics. Full Release CTest: Windows 44/44, WSL Ubuntu 39/39. See [06.55 report](reports/06.55-conflict-copy.md).

## D119 — Unicode path and manual dialog verification (06.56)

- Date: 2026-11-16.
- Add an integration fixture whose parent directory is `Проверка копии Ω`; verify byte-exact copy and unchanged conflicting occupant, malformed journal and candidate slot. Keep the fixture under the test-owned temporary root.
- Provide opt-in `--smoke-inspect-copy-dialog` to hold the real Editor confirmation modal open after constructing the existing isolated smoke fixture. Cancel is the required manual action for this review; it verifies destination absence and source/journal/slot preservation before the normal smoke cleanup.
- The inspection flag is test harness behavior only. It adds no public runtime feature or save format change and never opens the user's project/save directory.
- Visual inspection and exit code 0 were verified on Windows; automated integration passed on Windows and WSL Ubuntu. Do not claim visual validation on WSL, macOS, other POSIX systems or mobile targets. Full Release CTest: Windows 44/44, WSL Ubuntu 39/39. See [06.56 report](reports/06.56-unicode-copy-and-dialog-review.md).

## D120 — macOS/POSIX copy-aside portability assessment (06.57)

- Date: 2026-11-16.
- Keep the current descriptor-relative POSIX transaction unchanged in this assessment. Apple documentation confirms the relevant no-follow/directory open options, advisory `flock`, and atomic hard-link creation constraints. This makes the API path plausible for Darwin but is not evidence of a successful macOS build or runtime behavior.
- Treat `flock` as coordination among cooperating processes only. Same-directory staging and publication avoid cross-filesystem link failure by construction, but concrete volumes and providers still need testing.
- Do not describe the existing `fsync` calls as power-loss durable. Apple's `fsync(2)` documents that data can remain in device caches; `F_FULLFSYNC` is platform-specific and has filesystem/device limitations. Directory `fsync` behavior on supported macOS filesystems must be observed on actual macOS before setting the user-visible error/durability contract.
- Before advertising macOS support for this path, run focused and full Release tests on a supported macOS host/APFS: exact bytes and no-replace publication, destination collision, Unicode paths, symlink rejection, lease contention, directory/file flush outcomes, and subprocess interruption. Do not claim kernel-crash or power-loss safety based on these tests.
- 06.57 was documentation-only. Windows Release CTest passed 44/44; WSL Ubuntu Release CTest passed 39/39. Neither result is a macOS test. See [06.57 assessment](reports/06.57-posix-macos-portability-assessment.md).

## D121 — Explicit time-count conversion in cross-platform tests (06.58)

- Date: 2026-11-16.
- The user-provided macOS build result reported ambiguous `std::to_string` overloads in the C# code-workspace and build-manifest unit tests. Both formatted `std::filesystem::file_time_type::duration::rep` directly, whose underlying type can vary across standard libraries/platforms.
- Cast the test-only clock count to `long long` before calling `std::to_string`, selecting an exact standard overload. This affects only temporary test-directory naming; no production API or saved data format changes.
- Windows Release build succeeded; focused tests passed 2/2 and full CTest 44/44. WSL Ubuntu Release build succeeded; focused tests passed 2/2 and full CTest 39/39.
- These checks are not a macOS rerun. Keep macOS status unconfirmed until the user rebuilds and reruns CTest there. The reported 39 missing-executable failures were downstream consequences of the failed build, not 39 independently observed test assertion failures. See [06.58 report](reports/06.58-macos-test-portability-fix.md).

## D122 — Keep 3D prototypes modular before dependency adoption (07.1 assessment)

- Date: 2026-11-16.
- Existing implementation is 2D-only and there is no existing mesh, 3D camera, glTF importer or 3D physics contract. Preserve `astro_core` and headless builds without SDL/bgfx/import/physics dependencies.
- Reuse the existing bgfx backend only behind an optional rendering adapter for the first procedural triangle/cube prototype. Expose backend-neutral mesh/camera data; do not expose bgfx handles through Scene or the public project format.
- cgltf (MIT, reviewed upstream version 1.15) is a candidate parser for a later bounded `.gltf`/`.glb` importer, not selected or vendored. External buffer/image URI resolution must remain project-root validated and go through the asset pipeline; reject unsupported extensions rather than silently dropping required data.
- Jolt (MIT, C++17, STL-only per upstream docs) is a candidate for a later optional 3D physics adapter. It has not been built in this project or in CI and is not approved as an unconditional dependency. Box2D remains 2D-only for this decision.
- No versions are pinned and no dependency was downloaded. First coding slice: camera/projection and backend-neutral mesh contracts plus a visual bgfx smoke and baseline metrics. Then importer and physics are separate slices with their own security/performance tests. Do not alter Scene serialization or claim 3D support before those tests and migration conventions exist.
- Upstream sources reviewed 2026-11-16: [bgfx repository and BSD-2-Clause](https://github.com/bkaradzic/bgfx), [cgltf parser](https://github.com/jkuhlmann/cgltf/blob/master/cgltf.h) and [MIT license](https://github.com/jkuhlmann/cgltf/blob/master/LICENSE), [Jolt repository/build/platform notes](https://github.com/jrouwe/JoltPhysics) and [MIT license](https://github.com/jrouwe/JoltPhysics/blob/master/LICENSE), [Khronos glTF 2.0.1 spec](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
- See [07.1 assessment](reports/07.1-3d-foundation-assessment.md).
- Parser candidate status is superseded by the security/dependency review in [D124](#d124--bounded-gltf-parser-candidate-and-dependency-gate-071b): the local cgltf 1.15 copy is not approved for importer use; fastgltf v1.0.1 is the recommended candidate for a separately gated prototype.

## D123 — 3D camera/mesh prototype boundary (07.1a)

- Date: 2026-11-16.
- Keep the first 3D contract in `astro_runtime_render3d`, without a dependency from `astro_core`, Scene serialization or Editor. The data API uses right-handed camera coordinates (+Y up, forward -Z), column-major matrices, selectable [0,1]/[-1,1] projection depth, and validated indexed triangle lists.
- The optional renderer smoke is isolated under the existing Windows SDL3/bgfx build and initially targets D3D11. Reuse cached upstream bgfx shader binaries at build time; copy them only into the ignored build output, do not vendor source or generated shaders into the repository.
- This smoke validates coarse pixel presence and composition bounds through bgfx readback, but it is not a golden-image comparison. Do not claim production scene rendering or cross-platform 3D until exact image expectations and another separately built backend are validated.
- Keep glTF/GLB import, normals/materials, Scene component/versioning, Editor viewport integration and 3D physics as later independent slices with their own security, migration and regression coverage. No new external package was installed.
- Windows Release build: 161/161 actions; CTest 46/46. WSL Ubuntu Release build and CTest 40/40. Details: [07.1a report](reports/07.1a-3d-camera-mesh-prototype.md).

## D124 — Bounded glTF parser candidate and dependency gate (07.1b)

- Date: 2026-11-16.
- Do not adopt the `cgltf.h` found under bgfx's third-party directory for project asset import. The reviewed upstream cgltf 1.15 source has public unresolved memory-safety reports, including integer-overflow/OOB-read claims in validation; input files must be treated as untrusted. This is a scoped dependency risk finding, not a claim that all cgltf releases or every code path are affected.
- Evaluate released `fastgltf` v1.0.1 as the next prototype candidate: C++20, MIT. Its pinned CMake revision defaults `FASTGLTF_DOWNLOAD_SIMDJSON` and `FASTGLTF_DOWNLOAD_SIMDUTF` to ON and selects simdjson 5.1.0 and simdutf 9.3.3. A build must prevent unexpected downloads and either provide verified local sources or use explicit dependency acquisition in an ignored build cache; examples/tests and their glm/Catch2 fetches stay disabled unless separately needed. The reviewed licenses are MIT for fastgltf and the two embedded parsing/encoding dependencies.
- Candidate selection is not production approval. The importer must bound source, buffers, counts and decoded geometry; reject URI traversal, absolute/out-of-project paths and unsupported required extensions; confine all external resources to the canonical project root; preserve source bytes; publish derived cache atomically; and cover malformed/untrusted inputs, missing buffers, cache corruption and repeat import. Run fuzz/sanitizer coverage when toolchain support exists.
- No dependency download, vendoring, importer implementation, Scene format change or public API change occurred in this assessment. Permission for a local ignored build-cache acquisition is required before the next implementation slice under the project rule for installations/downloads.
- Windows Release verification remained 161/161 build actions and CTest 46/46 after the 07.1a readback extension. See [07.1b assessment](reports/07.1b-gltf-importer-dependency-assessment.md).

## D125 — Optional bounded glTF static mesh importer (07.1b)

- Date: 2026-11-17.
- Implement the first importer slice as a separate opt-in `astro_gltf` target; default headless configurations must neither locate nor fetch its third-party dependencies. Use fastgltf v1.0.1 at commit `87cfbceeeda127ccc9092694279437b700046119`, with simdjson 5.1.0 and simdutf 9.3.3 staged only in the ignored local build cache. CMake verifies the pinned commit and four source-file hashes, and explicitly disables fastgltf's dependency downloads and examples/tests/docs/install.
- The first supported product is backend-neutral static triangle geometry: float VEC3 POSITION plus optional unsigned indices. Import is explicit and returns `MeshData`; do not change Scene/AssetRegistry formats or claim general model support in this slice.
- Accept source files only under `Assets/`; constrain plain external buffer URIs to regular files below that directory; enforce explicit byte/count limits; reject unsupported required extensions and unsupported vertex/morph/compression data rather than silently losing it. Preserve source files. Use a versioned, checksummed derived cache under `Library/Derived/gltf` and repair a corrupt generated cache on reimport.
- Windows x64 Release opt-in build passed 95/95 Ninja actions and full CTest passed 42/42. Default headless Release configure/build passed with the option OFF and without an `astro_gltf` target. Focused adversarial integration test passed 1/1. These checks do not establish fuzz/sanitizer safety or other-platform support. Filesystem path inspection is not safe against hostile concurrent replacement races.
- The old no-download assessment is historical and remains useful for its candidate-selection rationale; implementation details and remaining limits are in the [07.1b report](reports/07.1b-bounded-gltf-importer.md).

## D126 — Scene model asset references (07.1c)

- Date: 2026-11-17.
- Add a minimal `MeshRenderer` Scene component that references a stable model AssetId and selects a mesh/primitive. Keep imported geometry backend-neutral; do not add renderer handles or third-party types to Scene or serialized project data.
- Advance Scene serialization to v6 and Asset index to v3 while retaining readers for the prior supported versions. Component changes use the typed command API and preserve component identity for undo/redo.
- Register models only through an explicit successful `GltfImporter::importAndRegister` call. Persist the top-level source size/hash and importer content digest. Reimport is the freshness check for external buffer bytes; no independent per-buffer dependency record is introduced in this slice.
- Prefab capture rejects MeshRenderer until prefab format/override semantics are implemented, preventing silent data loss. Scene/Game View rendering is also deferred.
- Windows x64 Release clean build passed 158/158 actions; focused integration passed 5/5; final full CTest passed 42/42 in 7.94 s. Fresh default headless Release build with importer/testing disabled passed 32/32 actions. macOS/mobile, fuzz/sanitizer and visual renderer validation remain unverified. See [07.1c report](reports/07.1c-scene-model-asset-reference.md).

## D127 — Scene MeshRenderer resolution (07.1d)

- Date: 2026-11-17.
- Resolve Scene geometry in the optional 3D/import adapter rather than adding importer or renderer-library dependencies to Scene or `astro_core`. The resolver accepts a `MeshRendererComponent`, returns backend-neutral selected `MeshData`, mesh name and visibility, and performs no drawing.
- Check component schema and nonzero registered AssetId; validate the model source with AssetRegistry; import current inputs and compare the persisted importer digest so external-buffer changes cannot silently render stale registered content; then validate mesh and primitive selectors. Return clear errors on stale/missing/invalid references. Reimport/register explicitly to update the content digest while retaining the AssetId.
- Keep the resolver stateless and return owned geometry; moving the selected primitive avoids copying its vertex/index arrays. No per-buffer graph or long-lived decoded geometry cache is added.
- Windows x64 Release clean build passed 159/159 actions; focused integration passed 1/1 and final full CTest passed 42/42 in 2.51 s. The headless configuration reconfigured/built with importer/testing disabled and contained no 3D/import targets. macOS/mobile, fuzz/sanitizer and visual rendering remain unverified. See [07.1d report](reports/07.1d-scene-mesh-resolution.md).

## D128 — Standalone Scene Mesh BGFX preview (07.1e)

- Date: 2026-11-17.
- First consume Scene model references in a standalone opt-in Windows D3D11/BGFX preview/smoke instead of extending the large Editor viewport path in this substage. The sample imports/registers a model, loads a versioned Scene fixture, resolves its MeshRenderer and submits its selected primitive.
- Keep the existing procedural cube smoke unchanged and available with glTF importer OFF. Register Scene preview CTest only when both the BGFX sample and glTF importer are enabled. CTest copies fixture files into a unique build-local temporary project and removes only that test-owned directory.
- Bound the sample uploader to 65,536 vertices and 196,608 indices for 16-bit index buffers. It previews one visible primitive at the origin, uses no Scene transform, disables back-face culling for the selected primitive and checks coarse framebuffer coverage/bounds rather than a golden image.
- Windows x64 Release focused graphics smoke passed 2/2; new Scene mode read back 24,642 foreground pixels with bounds (369,210)–(590,430). Final post-regeneration CTest passed 45/45 in 6.10 s (an earlier full run also passed in 12.96 s). Importer-off procedural smoke passed 1/1; headless configuration remained free of 3D targets. Editor/Game View/runtime integration and non-Windows backends remain unverified. See [07.1e report](reports/07.1e-scene-bgfx-preview.md).

## D129 — Editor Scene View Mesh preview (07.1f)

- Date: 2026-11-17.
- Connect the existing optional importer/resolver to Editor Scene View only. Keep `astro_gltf` and shader binaries conditional, so importer-off Editor builds retain the existing 2D path. Do not connect Game View or gameplay runtime in this slice.
- Reuse the Scene View BGFX color target with a depth attachment and a dedicated ordered 3D pass. Keep per-entity static vertex/index buffers across frames and destroy them on reference/entity removal or viewport teardown; recreate framebuffer attachments on resize. Provide explicit cache invalidation because the current asset registry has no filesystem watcher or importer digest notification to the Editor.
- Compose local transforms through parent hierarchy, rejecting missing transforms, non-finite values, zero scale, missing parents and cycles. The Editor smoke verifies a child origin after a parent scale and local offset, in addition to one submitted mesh draw.
- This remains a Windows D3D11 prototype: fixed camera, position/color-only triangles, 65,536 vertex / 196,608 index upload cap, and no materials/normals/lighting/picking. Do not describe this as Game View or runtime 3D support.
- Windows x64 Release importer-on build succeeded; focused Editor/BGFX smoke passed 3/3; full CTest passed 49/49. Importer-off Editor build succeeded and focused Editor smoke/localization passed 2/2. The new UI labels are covered by localization tests. No macOS/mobile backend or golden-image comparison was run. See [07.1f report](reports/07.1f-editor-scene-mesh-preview.md).

## D130 — Scene View camera and Frame All (07.1g)

- Date: 2026-11-17.
- Keep camera state and orbit/pan/zoom/frame mathematics in an Editor-only backend-neutral module; no renderer or UI dependency is required to unit-test it. Consume mouse controls only when the rendered Scene View viewport is hovered.
- Frame visible Scene meshes using the world-space AABB formed from all eight local-bounds corners after the complete hierarchy world transform. Reject a mesh's bounds as a whole when transformed coordinates are non-finite or outside the Scene View range; do not merge partial corner results.
- Preserve current position/color-only mesh pass and limit this feature to Scene View. Game View/runtime 3D, materials, lighting, picking, and cross-platform backend support are separate work.
- Windows x64 Release focused tests passed 5/5 in 5.02 s and complete CTest passed 50/50 in 9.82 s. Direct Editor smoke reported 139 3D submissions, child world origin `(2.75,0,0)`, and `frameAll=1`. This validates mathematical fit and submission/lifecycle, not visual pixel equivalence. See [07.1g report](reports/07.1g-editor-scene-camera.md).

## D131 — Scene View visual smoke and mesh diagnostics (07.1h)

- Date: 2026-11-17.
- Extend Editor smoke only with a test-owned deterministic scene containing two valid mesh references and one unresolved AssetId. Require valid submissions to continue despite the unresolved component and verify the diagnostic explicitly.
- For visual evidence, capture the existing BGFX window swapchain and crop the Scene View image using its normalized viewport rectangle. Require a small near-white pixel coverage floor for the white glTF fixture. Do not commit a pixel golden or treat the observed count as portable: BGFX screenshot capture in this integration accepts a window framebuffer, and rasterization varies by driver.
- Preserve existing user/project resources; the test fixture is copied/generated only under the isolated smoke directory and removed after testing.
- Windows x64 Release focused CTest passed 5/5 in 4.85 s; full CTest passed 50/50 in 9.47 s. The direct smoke observed two submissions in the last frame, one missing-AssetId diagnostic, Frame All center x=4, and 11,900 near-white viewport pixels. See [07.1h report](reports/07.1h-editor-scene-visual-smoke.md).

### D132 — Editor 3D frame profile scope (07.1i)

Keep 07.1i as an opt-in measurement mode (`astro_editor.exe --benchmark-3d`) using fixed 1/100/500/1000-mesh Scene View projects, explicit warm-up and measured frame counts, and separate CPU mesh preparation/submission/full-editor frame metrics. Report the BGFX GPU timer only as a full-window-frame timer because it spans the frame and is affected by VSync; do not interpret it as isolated Scene View GPU cost. Record process memory, fixture import/setup, and executable size as context. No performance thresholds are selected here; establish target hardware, visual quality, and budgets before adding pass/fail performance limits. The profile is an Editor smoke/diagnostic, not an optimizer or cross-platform benchmark. See [07.1i report](reports/07.1i-editor-3d-frame-profile.md).
### D133 — Material authoring contract before implementation (07.2a)

Keep material values/backend-neutral and keep GPU objects out of serialized Scene and project asset data. The eventual authoring model must represent reusable material assets, glTF per-primitive source slots, and explicit project overrides separately; texture roles and stable AssetIds must remain inspectable and diagnosable. Use a metallic-roughness baseline with explicit color-space/channel semantics, alpha mode/cutoff, normal/occlusion/emissive inputs, and editable base-color/metallic/roughness/emissive values. Material edits must flow through the typed command/transaction path and support Undo/Redo. Lighting/environment and shadow state are separate from material identity. This assessment intentionally does not select an on-disk schema, public API, shader representation, feature budget, or dependency. Before PBR rendering, add missing normals/UV/material-slot import and stable tests; choose a versioned material format only in a later implementation substage. See [07.2a assessment](reports/07.2a-material-authoring-contract.md).

### D134 — Versioned material asset and registry (07.2b)

Represent reusable material values as `MaterialAssetData` in strict UTF-8 `ASTRO_MATERIAL 1` files below `Assets/Materials`, with backend-neutral factors and typed image AssetId/texCoord bindings. Resolve registered texture IDs through the existing image registry and preserve source files; use source size/SHA-256 for stale detection. Add `MATERIAL` records to the shared monotonic-ID asset index as v4, retaining readers for index v1–v3 and upgrading only on explicit registry mutation. Keep GPU state, Scene binding, command history, glTF source slots, overrides, rendering and authoring UI outside this slice. Do not add a parser dependency. See [07.2b report](reports/07.2b-material-asset-and-registry.md) and [file format](FILE_FORMATS.md#material-asset-v1-072b).

### D135 — Bounded glTF material metadata import (07.2c)

Keep source glTF material slots and reusable `.astro-material` assets as separate data layers. Extend the optional `astro_gltf` result with supported core metallic-roughness values, texture-role -> glTF texture/image references and the optional source material index per primitive. Do not mint AssetIds or rewrite source assets in this importer slice. Hash bounded external image bytes into the import freshness digest; resolve local image references only beneath `Assets/`, never fetch remote URIs, and diagnose missing/unsupported references/extensions while retaining their source mapping where safe. Preserve embedded images as opaque source metadata rather than decoding them. Use fastgltf plus its already-staged simdjson dependency; no new third-party dependency. Keep Scene slots, renderer, Inspector, overrides, normals/UVs and lighting out of scope. See [07.2c report](reports/07.2c-gltf-material-metadata.md).

### D136 — Scene material-slot override and v7 migration (07.2d)

Store one optional project Material AssetId override on the Scene `MeshRenderer` primitive selection. Keep the importer-provided glTF source material index independent, and treat AssetId zero as “use source material.” Add a transactional typed command so assignment and clearing participate in validation, audit and Undo/Redo. Advance Astro Scene to v7 and MeshRenderer schema v2; continue reading Scene v1–v6 and default legacy v6 mesh components to no override. Resolve nonzero overrides against AssetRegistry before reporting a bound material as usable. This stage defines and validates authoring references only; it does not imply that Editor Inspector or rendering consumes them. No new dependency. See [07.2d report](reports/07.2d-scene-material-bindings.md) and [Scene v7 format](FILE_FORMATS.md#astro-scene-v7).

### D137 — Inspector material choices and cached slot diagnostics (07.2e)

Expose a `MeshRenderer` primitive's source glTF material index and separate project override in the Editor Inspector. Populate choices only from `AssetRegistry::materialEntries`; validate a newly chosen asset with `resolveMaterial` before executing `entity.set_mesh_material_override`, and route clears through the same command. Show missing/unregistered and stale/corrupt references without silently replacing them. Cache importer/asset validation by selected component and project, with an explicit Refresh control, so the Editor does not reread assets every frame. The Inspector is authoring UI only and does not imply GPU material rendering or parameter editing. Preserve the optional-importer build behavior and localize all new UI labels. See [07.2e report](reports/07.2e-meshrenderer-material-inspector.md).

### D138 — First BGFX material draw prototype scope (07.2f)

Keep this first rendering experiment isolated to the optional Windows BGFX sample. Resolve an explicit Scene material override from AssetRegistry and multiply its linear `baseColorFactor` into the existing vertex-color bytes before upload; preserve the existing position/color shader and validate the resulting red-dominant screenshot with a separate CTest. This is a flat tint diagnostic, not a color-managed presentation contract or PBR implementation. Do not imply support for metallic/roughness/emissive/alpha, texture roles, normals/UV, lights, shadows, Editor Scene View, Game View, runtime, or other platforms. The importer still rejects NORMAL/TEXCOORD attributes; a later substage must add and validate those vertex channels before normal-based shading or texture sampling. No new dependency. See [07.2f test record](TESTING.md#072f-first-bgfx-material-draw-path).

### D139 — Optional mesh channels and bounded glTF import (07.2g)

Extend backend-neutral `MeshVertex` with normal and UV0 values, but preserve channel availability separately in `MeshData` so absence is never represented by fabricated shading data. Import only glTF `NORMAL` (non-sparse FLOAT VEC3) and `TEXCOORD_0` (non-sparse FLOAT VEC2 or normalized UNSIGNED_BYTE/UNSIGNED_SHORT VEC2); require matching vertex counts and accessor bounds. Reject duplicate/unrecognized attributes, sparse accessors, zero/non-finite normals, unsupported formats and UVs beyond channel 0. Keep source bytes unchanged. Bump the derived geometry cache format to v2 and use a distinct filename; leave old v1 cache files untouched and miss them naturally. No shader, texture, Editor or external dependency work in this step. See [07.2g report](reports/07.2g-normal-uv-mesh-import.md).

### D140 — Single base-color texture prototype (07.2h)

Share the bounded Windows WIC decoder between SDL image textures and the isolated BGFX prototype. Keep decoding restricted to the verified AssetRegistry cache path; enforce finite dimensions, a 16 Mi-pixel cap, BGRA8 output and safe buffer sizing. Add a separate D3D11 smoke proving one registered `BASE_COLOR` image maps to imported UV0 and reaches the rendered screenshot. Do not imply generalized texture roles, color management, PBR, or Editor/Game View integration. The fixture uses a white baseColorFactor so the current sample can isolate texture sampling. No new dependency; use the already staged BGFX D3D11 texture shaders. See [07.2h test record](TESTING.md#072h-base-color-texture-smoke) and [report](reports/07.2h-safe-base-color-texture-smoke.md).

### D141 — Linear base-color factor over sRGB source (07.2i)

Define this isolated base-color path as sRGB-encoded RGB source bytes plus a linear RGBA factor. Decode RGB with the IEC 61966-2-1 piecewise transfer, multiply in linear space, multiply alpha directly as linear, and round the temporary upload copy to linear BGRA8 UNORM. Never rewrite source assets or the content-addressed cache. The Windows D3D11 smoke samples this prepared linear texture and enables `BGFX_RESET_SRGB_BACKBUFFER` for display encoding; it keeps the 07.2h white-factor texture smoke unchanged. Unit tests cover transfer/validation and a second BGFX smoke checks expected encoded colors. This validates CPU preprocessing plus sRGB output only; it does not validate GPU sRGB texture views, cross-platform behavior, filtering/mips, alpha blending, other roles, Editor/Game View, or PBR. No new dependency. See [07.2i tests](TESTING.md#072i-base-color-color-space-and-factor) and [report](reports/07.2i-base-color-color-space.md).

### D142 — Editor Scene View base-color material preview (07.2j)

Connect the previously tested base-color preparation path to the optional Windows Editor Scene View for explicit registered material overrides. Resolve images only through AssetRegistry and its WIC-bounded derived cache; keep source and cache bytes immutable. Prepare factor-only materials from a temporary white texel. Use the texture shader only with UV0 and valid material/image/factor/upload state; otherwise submit the mesh through the existing vertex-color shader and expose a diagnostic. Keep GPU texture ownership in the Editor Scene View entity cache. Scope is one base-color role and one override path; Game View, imported source-material selection, other PBR roles, alpha policy, sampler transforms, mipmaps, lighting, shadows and non-Windows renderers remain unimplemented. Do not add dependencies. Smoke checks textured draw, fallback draw, diagnostics and screenshot color presence; it is not an exact image golden. See [07.2j tests](TESTING.md#072j-editor-scene-view-base-color-preview), [3D rendering](3D_RENDERING.md#editor-scene-view-base-color-material-preview-072j), and [report](reports/07.2j-editor-scene-view-base-color.md).

### D143 — Editor Scene View source material and override priority (07.2k)

Use a deterministic selection rule per glTF primitive: explicit registered project material override first; otherwise its imported glTF source material; otherwise imported vertex colors. Keep source slot/index and project AssetId separate. A present but unresolved explicit override is an error: keep the mesh visible through vertex-color fallback and diagnose it rather than silently switching to source. Consume only baseColorFactor and an available local external baseColor image on UV0. Resolve source image paths only under project `Assets`, decode through bounded WIC, do not register/import during preview, and never rewrite source bytes or fetch remote URIs. Embedded/data-URI, missing/unsupported images, UV sets other than 0, invalid data or render failure use diagnosed vertex-color fallback. Keep this as Windows D3D11 Scene View behavior; no Game View or other PBR roles. No new dependency. Editor smoke covers source, project override, UV fallback and invalid override plus screenshot color coverage. See [07.2k tests](TESTING.md#072k-editor-scene-view-source-material-policy), [3D rendering](3D_RENDERING.md#scene-view-source-material-and-project-override-policy-072k) and [report](reports/07.2k-scene-view-source-material-policy.md).

### D145 — glTF base-color sampler and UV transform assessment (07.2m)

Assessment only; no runtime behavior, shader, asset format, API, dependency or test fixture changes. Khronos glTF sampler wrap is independently specified for U/V; omitted samplers mean REPEAT plus implementation-selected filtering. Explicit minification may select mipmap modes, while the current Editor upload creates one mip level. `KHR_texture_transform` adds offset, rotation, scale and an optional `texCoord` override on an individual texture-info binding, so it is distinct from a texture's sampler. The current importer retains only the sampler index, current Scene View binds Clamp + Point flags, and geometry exposes UV0 only. Recommend a bounded follow-up for the base-color Scene View path: carry validated sampler values and per-binding transform through source-material resolution; honor REPEAT/CLAMP/MIRRORED_REPEAT, transform UV0 without mutating imported vertices, and keep unsupported UV sets or invalid/mip-dependent settings on the diagnosed vertex-color fallback. Define defaults explicitly and resolve whether project material overrides need a versioned texture-sampling field before claiming parity with source glTF. Do not silently ignore `extensionsRequired`. Add importer unit/integration coverage for defaults, enum values, bounds, transform defaults/override and unsupported required features; add D3D11 Editor smoke patterns for wrap/filter/offset/scale/rotation and fallback. No new dependency is required. Game View, other texture roles, broader UV channels, platform parity and full PBR remain out of scope. See [07.2m assessment](reports/07.2m-gltf-sampler-and-uv-transform-assessment.md), [testing](TESTING.md#072m-gltf-sampler-and-uv-transform-assessment) and [3D rendering](3D_RENDERING.md#gltf-base-color-sampler-and-uv-transform-assessment-072m).

### D144 — Inspector preview source and material feature limits (07.2l)

Expose the active Scene View choice in the `MeshRenderer` Inspector: valid explicit project override, glTF source material when no override exists, or imported vertex colors when no material is assigned. For a valid override with a source slot, say that clearing the override returns to that source. Label an unresolved override as vertex-color fallback. State that the current preview consumes only base-color factor and local UV0 textures; alpha, sampler transforms and other PBR properties are not rendered. Keep assignment/clear actions on the existing `entity.set_mesh_material_override` typed transactional command; do not change schemas or command API. Smoke the command assignment and clear on a test-owned entity, assert both states, and check localized labels/limits. GUI mouse automation, other platforms, Game View and additional material behavior remain outside this step. No new dependencies. See [07.2l testing](TESTING.md#072l-editor-inspector-material-preview-authoring), [3D rendering](3D_RENDERING.md#inspector-preview-source-and-feature-limits-072l) and [report](reports/07.2l-inspector-material-preview-authoring.md).

### D146 — Source glTF base-color sampler and UV transform (07.2n)

Extend import metadata with validated glTF sampler wrap/filter values and `KHR_texture_transform` per texture-info binding. Enable the existing fastgltf extension parser; the extension's optional `texCoord` overrides the core texture binding coordinate. Keep the Scene View implementation to source glTF base-color on UV0: map all three wrap modes and nearest/linear non-mip filters to BGFX per-draw flags, selecting linear/repeat for omitted values according to BGFX defaults. Keep all mipmapped `minFilter` modes on diagnosed vertex-color fallback until a mip chain is implemented. Apply scale, counter-clockwise rotation and offset to a CPU copy used for the GPU vertex buffer; never mutate imported geometry, derived cache or source files. Preserve project `.astro-material` format and its previous Clamp + Point/no-transform preview policy; this is an explicit feature gap for a later material-authoring step. Nonzero UV sets, any file declaring `extensionsRequired`, other material roles, alpha, Game View and non-Windows renderers remain unsupported. Add importer integration tests, transform unit tests, localization checks and Editor BGFX smoke assertions. No new dependency. See [07.2n report](reports/07.2n-source-gltf-sampler-uv-transform.md), [3D Rendering](3D_RENDERING.md#source-gltf-base-color-sampler-and-uv-transform-072n), and [TESTING](TESTING.md#072n-source-gltf-base-color-sampler-and-uv-transform).

### D147 — Project material sampler and UV authoring (07.2o)

Add base-color wrap U/V, min/mag filter, and UV offset/rotation/scale to `.astro-material` v2. Continue reading v1 with Clamp + Nearest and identity-transform defaults; do not rewrite it on registration/open. The Inspector edits a draft and persists only on an explicit save. Save validates current source hash and image AssetIds, atomically replaces the material source, then refreshes the registry version/hash while retaining the stable AssetId; reject an outside edit if the registered source hash is stale. Scene View applies the same tested single-mip sampler and UV0-copy path to project overrides as source materials. Mip minification stays a diagnosed vertex-color fallback. No Scene/command schema changes or new dependencies. Other texture roles, Game View, generalized PBR, platform parity and pixel-golden testing remain out of scope. See [format](FILE_FORMATS.md#material-asset-v1-and-v2-072b-072o), [3D rendering](3D_RENDERING.md#project-material-sampler-and-uv-transform-authoring-072o), [testing](TESTING.md#072o-project-material-sampler-and-uv-authoring), and [report](reports/07.2o-project-material-sampler-authoring.md).

### D148 — Bounded base-color mips and shared texture cache (07.2p)

Generate complete 2D mip chains by area-filtering the already linearized base-color BGRA8 image. Keep source bytes untouched. Map each of the six glTF/project minification modes to BGFX min-texel and mip-point flags, and upload a full mip chain only when the selected mode requests mips. A cache key includes decoded image bytes, dimensions, linear factor and mip-chain policy; mesh entries hold references and the GPU texture is destroyed when the final reference is released. Bound the source to 16 Mi pixels, each packed chain to 96 MiB and live Scene View cache to 256 MiB. On invalid/oversized data, failed upload or full cache, retain geometry and show a diagnostic with vertex-color fallback. glTF omitted minFilter uses the glTF linear non-mip default. Preserve `.astro-material` schemas and v1 defaults. No new dependency or source asset/geometry mutation. Other texture roles, alpha/blending, Game View and non-Windows backends remain outside scope. See [07.2p tests](TESTING.md#072p-bounded-base-color-mip-chains-and-shared-cache), [3D rendering](3D_RENDERING.md#bounded-base-color-mip-chain-preview-072p), and [report](reports/07.2p-bounded-mip-chains.md).

### D149 — Scene View alpha mode and depth-order assessment (07.2q)

Assessment only; no implementation, format/API/dependency changes or new tests. Existing project materials persist validated `alphaMode`, `alphaCutoff` and `doubleSided`; glTF import carries the same fields, but Scene View selects none of them when rendering. The current prebuilt fragment shader simply returns sampled RGBA; the draw state enables RGB/A/Z writes and depth-test-less, without blending or alpha discard. Mesh draws are submitted in Scene entity order to a BGFX sequential view. Define the next rendering slice as follows: OPAQUE ignores source alpha and outputs alpha 1, with depth test/write and no blending; MASK discards when combined base-color alpha is below `alphaCutoff`, writes depth only for surviving fragments and uses no blending; BLEND uses straight-alpha Porter-Duff over, depth-tests against opaque/masked content, disables depth writes, and renders after those queues in stable back-to-front order by camera-space object-center depth with Asset/Entity ID tie-breaking. Preserve sequential view mode and construct the order explicitly on CPU rather than applying one BGFX depth-sort mode to the mixed opaque/transparent pass. Use separate RGB and alpha blend factors when preserving render-target alpha. Keep object-level sorting explicitly approximate: intersecting or self-overlapping transparent triangles are not correctly ordered; OIT, per-triangle sorting and alpha-to-coverage are deferred. Existing mip generation averages straight RGB and alpha independently, so transparent-color fringes and MASK coverage shifts need tests; define whether the initial slice documents these limits or adds alpha-aware mip policies before enabling alpha modes. `doubleSided` is already stored but is not represented by explicit cull flags in the current Scene View state; explicitly set/validate culling policy in implementation. Keep source image/material formats unchanged. See [assessment report](reports/07.2q-alpha-blending-depth-assessment.md), [3D rendering](3D_RENDERING.md#scene-view-alpha-mode-and-depth-order-assessment-072q), and [testing](TESTING.md#072q-alpha-mode-and-depth-order-assessment).

### D170 — проверяемая геометрия Editor окна и compact smoke (07.2ak)

Keep display geometry as a pure `EditorWorkspaceLayoutPolicy::constrainWindow` calculation, then apply that result only in response to the current SDL usable bounds and display/scale/resize events. Test negative monitor origins, out-of-bounds positions and oversized windows with unit tests; validate the SDL event-to-window effect in a dedicated Editor smoke. Do not rewrite saved dock layouts on monitor transitions. The compact spatial MASK screenshots validate 3D mesh, texture and alpha output; their 2D color markers may be correctly occluded by opaque meshes, so the generic color-marker check remains on the ordinary Editor smoke and 2D composition remains separately covered by runtime integration. No rendering, scene format or external dependency changes. Current Windows full Release CTest passed 87/87 at SDL displayScale 1.25; a 100% full run and real mixed-DPI monitor move remain unverified. See [tests](TESTING.md#072ak-compact-visual-smoke-и-sdl-геометрия-окна) and [report](reports/07.2ak-window-geometry-and-compact-smokes.md).

### D171 — проверять DPI переход реальным перемещением SDL окна (07.2al)

Добавить отдельный Editor smoke, который выбирает среди реальных SDL displays пару с различными content scale, переносит окно через их usable bounds в обе стороны и проверяет итоговые `SDL_GetWindowDisplayScale()` и применённый ImGui scale. Display/scale events остаются диагностическими данными, но не являются обязательным условием обновления UI: подтверждённый Windows переход на scale 1.0 не выдал events, а per-frame polling обновил scale правильно. Если разномасштабных дисплеев нет, тест должен завершаться CTest skip, не ложным pass. Не менять сохранённые dock layouts, Scene форматы, публичный API или зависимости. На текущем Windows хосте smoke прошёл 3 повтора, полный Release CTest — 88/88 при 1@1.25 и 2@1.0; восстановление глобального 100% через стандартный UI не подтверждено, обход custom scale не выполнялся. См. [TESTING](TESTING.md#072al-переход-окна-между-displays-с-разным-dpi) и [отчёт](reports/07.2al-mixed-dpi-window-transition.md).

### D150 — Bounded Scene View alpha pass (07.2r)

Implement OPAQUE, MASK and BLEND only in the existing Windows Editor Scene View, with no material/scene schema change. Keep the BGFX Sequential view and CPU-order draws OPAQUE -> MASK -> BLEND. OPAQUE forces fragment alpha to 1 and depth-writes; MASK discards combined sampled alpha strictly below cutoff and depth-writes survivors; BLEND uses straight-alpha over with separate RGB and alpha factors, depth-tests and does not depth-write. Sort blend items far-to-near using view-space object-center Z; use EntityId for deterministic ties and non-finite depths. Set `BGFX_STATE_CULL_CW` for single-sided materials and disable culling for `doubleSided`. Own the alpha fragment shader source and compile it to local D3D11 DXBC with the BGFX shaderc staged under `ASTRO_SPIKE_DEPS`; fail configuration with local tool-build guidance when shaderc is missing. Do not fetch or install tools through CMake. Because existing mip generation independently averages straight color and alpha, disable mip-chain creation for MASK/BLEND while retaining the base texel filter; OPAQUE keeps the selected mip policy. This prevents current alpha-mip coverage/fringe issues but can alias at distance. Keep source images, imported geometry and material files immutable. The Editor smoke combines a below-cutoff mask in front of a translucent mesh and verifies visible blended pixels. Object-center sorting cannot order intersecting/self-overlapping triangles. Game View, non-Windows backends, OIT, per-triangle sorting, PBR lighting and shadows remain deferred. No new third-party dependency. See [tests](TESTING.md#072r-scene-view-alpha-pass), [3D rendering](3D_RENDERING.md#scene-view-alpha-pass-072r), and [report](reports/07.2r-scene-view-alpha-pass.md).
### D191 — транзакционное создание/удаление 3D-примитивов (07.2be)

Иерархия вызывает Scene-команды `entity.create_primitive` и `entity.delete_subtree`, а не меняет документ напрямую. Create сохраняет стабильные EntityId/ComponentId при Undo/Redo; Delete транзакционно захватывает Scene+prefab-instance snapshot, чтобы отмена восстанавливала всё поддерево и метаданные. Это корректная граница для текущей стадии; цена — память пропорциональна полной сцене, а не только удалённой ветви. Отдельный Editor smoke отправляет все четыре примитива через BGFX Scene View и проверяет submission/diagnostics, без pixel golden. Windows x64 Release целевые тесты — 4/4 passed, полный CTest — 96 passed и 1 штатно skipped из 97 за 126,89 с; другие backend не запускались. См. [отчёт](reports/07.2be-primitive-transactions-render-smoke.md), [COMMAND_API](COMMAND_API.md), [TESTING](TESTING.md#072be-транзакционные-примитивы-и-scene-view-smoke).

### D222 — выбор поддерживаемого BGFX depth format и Vulkan Scene View smoke (07.2cj)

Для Scene/Game framebuffer выбирать первый формат глубины из поддерживаемых BGFX render-target capabilities: D24S8, D32F, D24, D16. Так сохраняется D24S8 на D3D11, когда он доступен, и Vulkan не отказывает в создании viewport, если формат отсутствует. Текущий Vulkan host не поддерживает D24S8/D24, но поддерживает D32F/D16; этот же путь работает на локальной GPU. Оставить D3D11 обычным renderer default. Vulkan тест ограничить Scene View: Game View assertions намеренно не включать. Добавить локализованное сравнение screenshot-derived marker pixel counts с `max(8, 5%)` допуском, а не общий full-frame golden. Зависимости и публичные Scene/API форматы не менять. Подтверждено только Windows x64/текущим AMD Vulkan устройством; для иных renderer/драйверов, Game View и платформ требуется отдельная проверка. См. [тесты](TESTING.md#vulkan-scene-view-material-smoke-072cj), [3D Rendering](3D_RENDERING.md#vulkan-scene-view-material-smoke-072cj), и [отчёт](reports/07.2cj-vulkan-scene-view-smoke.md).

### D223 — Vulkan Game View readback boundary (07.2ck)

Add an opt-in Vulkan Game View smoke using the existing deterministic runtime-camera, textured-mesh fixture. Keep D3D11 as default/control and Vulkan fallback disabled. Require actual Vulkan renderer selection, camera-driven runtime adapter and two texture submissions. Use the existing 32×24 framebuffer-to-final-window comparison at 8/255 channel tolerance and 99.5% match threshold. Current evidence: D3D11 768/768 exact; Vulkan 563/768 (best opposite vertical orientation 564/768), so Vulkan presentation/composition remains unverified. Do not weaken the threshold or describe the measured Vulkan image as parity. CTest may classify only this recognized Vulkan readback mismatch as skipped while preserving its metrics; unrelated initialization/submission failures remain failures. The exact color/presentation cause is unresolved and needs a separate corrective substage. No Scene/API/asset format changes or dependencies.

### D224 — Vulkan Game View color-transfer investigation (07.2cl)

Do not infer a transfer fix from sampler or shader mode names. Explicit `BGFX_SAMPLER_SRGB` regressed both backend readbacks to 0/768; BGFX ImGui sRGB output mode regressed Vulkan to 0/768. Revert both and preserve the passing D3D11 baseline and explicit Vulkan skip. Before another fix, capture the actual Vulkan swapchain format and compare a known color chart at attachment, sampling, and final-window stages. Keep the 99.5% threshold; add no dependency. See [07.2cl report](reports/07.2cl-vulkan-imgui-color-transfer.md).

### D225 — Vulkan surface format and known-color chart (07.2cm)

Keep D3D11 as the regular Editor backend and preserve the Vulkan Game View 99.5% readback contract. The selected surface on the current AMD host is `VK_FORMAT_B8G8R8A8_UNORM` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`; a four-color mutable BGRA8 chart sampled through ImGui matches the final window 4/4 within 8/255 (max delta 3). This rules out a generic failure of this simple ImGui texture-to-window path, but does not identify why the Game View attachment mismatches at 563/768. Keep that test skipped only for its recognized mismatch. Next diagnostic should compare stable Game View source texture samples with the texture as sampled, including UV orientation and target sizing. No API/schema or dependency change. See [07.2cm report](reports/07.2cm-vulkan-surface-format-and-color-chart.md).

### D226 — Vulkan Game View target/window tap diagnostics (07.2cn)

Add opt-in test-only reporting for Game View render-target size, BGFX backbuffer, final-window capture size, viewport rectangle, BGFX UV-origin flag, per-row matches and five fixed BGRA target/window probes. Keep the 99.5% comparison threshold and existing recognized mismatch skip unchanged; do not infer a color fix or parity from the diagnostic. Current host measures target 624×416, backbuffer 1100×720, screenshot 1280×800 and 563/768 matches (alternate Y 564/768). These dimensions are not yet reconciled, so coordinate mapping remains a possible confounder, not a proven cause. No production render behavior, public API, schema, or dependency change. Full Release CTest: 115 passed, 2 skipped, 0 failed of 117. See [07.2cn report](reports/07.2cn-game-view-target-window-taps.md).

### D227 — controlled Vulkan Game View screenshot sizing (07.2co)

Use separate opt-in 1100×720 and 1280×800 window-size tap smokes to test coordinate scaling. On the current AMD/Vulkan host, both BGFX screenshot TGAs are 1280×800. The 1100×720 run has a 726×462 screenshot viewport over a 624×416 target and matches 563/768 samples; the 1280×800 run has a 624×416 viewport equal to the target and matches 768/768 with maxDelta 0. Treat this as evidence that screenshot/window sizing is associated with the mismatch, not proof of a swapchain or coordinate bug. Keep the 99.5% threshold and D3D11 default. No production renderer/API change. See [07.2co report](reports/07.2co-vulkan-capture-coordinate-scaling.md).

### D228 — Vulkan surface and BGFX screenshot extent after resize (07.2cp)

At the tap smoke's 1100×720 capture point, SDL drawable pixels, the width/height passed to `bgfx::reset`, BGFX stats, and an independent Vulkan `VkSurfaceCapabilitiesKHR.currentExtent` query for the same HWND all report 1100×720. The TGA returned by BGFX screenshot capture is 1280×800; BGFX's Vulkan source passes its selected `SwapChainVK::m_sci.imageExtent` into the screenshot readback. At 1280×800, surface and TGA sizes agree and readback is 768/768. Therefore the current mismatch is narrowed to BGFX's screenshot target selection/path after resize, but the exact internal framebuffer is not identified. Keep this diagnostic-only; no production fix, API, schema, dependency, or relaxed 99.5% threshold. See [07.2cp report](reports/07.2cp-vulkan-swapchain-extent-after-resize.md).

### D229 — BGFX invalid-handle screenshot target (07.2cq)

Source trace resolves the target: BGFX stores the invalid screenshot handle, and Vulkan `getFrameBuffer(BGFX_INVALID_HANDLE)` returns `m_backBuffer`. `requestScreenShot` obtains the backbuffer's `SwapChainVK`, then `readSwapChain` uses that swapchain's stored `m_sci.imageExtent`. Thus the TGA dimensions describe BGFX's main backbuffer/swapchain, not the Game View target and not an arbitrary editor framebuffer. The capture remains 1280×800 in the 1100×720 resize test while SDL, BGFX stats and an independent query of the same HWND surface report 1100×720. This proves framebuffer selection, but not whether the stale value comes from resize timing, BGFX state, or the tested capture sequence. Preserve the diagnostic skip and 99.5% threshold. Do not patch vendor BGFX or change production sizing until a test isolates the stale-state transition. No API/schema/dependency change. See [07.2cq report](reports/07.2cq-bgfx-invalid-handle-screenshot-target.md).
### D230 — BGFX Vulkan shrink-transition trace (07.2cr)

Keep the Vulkan 99.5% parity threshold and existing recognized skip. The deterministic tap sequence now requests 1100×720 before resize, grows to 1280×800, then shrinks back to 1100×720. In the current Windows x64 AMD Vulkan run, a baseline BGFX screenshot measured 1100×720; four screenshots after growth measured 1280×800; four screenshots after shrink remained 1280×800. BGFX stats lag the shrink reset by one application frame (1280×800 at frame 46, 1100×720 at frame 47). This confirms the mismatch occurs on the shrink transition, but not the backend reason. A single interim run reported 768/768 parity; subsequent targeted rerun and full CTest retained 563/768 and the established skip, so the transient result is not accepted as a fix. No production renderer, public API, schema, dependency, or vendor changes. Next isolate `SwapChainVK::update` extent and swapchain-recreation state around shrinking. See [the 07.2cr report](reports/07.2cr-bgfx-resize-frame-capture-trace.md).
### D231 — Vulkan shrink swapchain recreation condition (07.2cs)

The current BGFX Vulkan resize failure is traced to descriptor/image-extent divergence on this Windows x64 AMD host. A surface transition to 1280×800 recreated the swapchain with `m_desc=1100×720`, while presentation scaling chose `m_sci.imageExtent=1280×800`. When the surface later returned to 1100×720, `_desc` again equaled stored `m_desc` and `m_needToRecreateSwapchain` was false; the backend's recreation predicate did not compare stored image extent or current surface extent, so no new swapchain was created. BGFX stats reflect the requested size and do not prove the screenshot extent. Keep D3D11 default, preserve the 99.5% Vulkan parity threshold and recognized skip, and require a fix smoke across grow/shrink and stable-size cases. Temporary instrumentation was restored after diagnosis. See [07.2cs report](reports/07.2cs-bgfx-vulkan-shrink-recreate-root-cause.md).

### D232 — BGFX Vulkan fixed-surface extent recreation (07.2ct)

Carry a small patch against the locally staged BGFX revision `eebf5686a8c5f1c77e1af2dad17b5746ba17da8f`: when BGFX evaluates a swapchain update with no existing recreation reason, query surface capabilities; if `currentExtent` is fixed and differs from the chosen `m_sci.imageExtent`, recreate the swapchain and attachments. Undefined/variable surface extents keep current BGFX behavior. Store the unified diff in the project and apply it with a script that validates the exact dependency revision and refuses unrelated checkout changes; do not edit or fetch dependencies as part of ordinary CMake configure. Acceptance requires stable-size, grow, shrink-back TGA extent checks and the existing 99.5% readback threshold on the current host. Observed result: nine expected extents (1100×720 baseline, four 1280×800 grow frames, four 1100×720 shrink frames), 768/768 readback matches, and 117 Release CTests passed with one unavailable mixed-DPI skip. No public API or scene format change. Keep D3D11 default and mark other Vulkan drivers unverified. See [07.2ct report](reports/07.2ct-bgfx-vulkan-resize-fix.md).

### D233 — BGFX patch clean-copy build (07.2cu)

Verify dependency patches from clean local clones when the source is already cached, and keep that validation isolated under ignored `build/` paths. For 07.2ct, fresh BGFX/BX/BIMG checkouts at their pinned commits accepted the patch and produced a Release BGFX library; a second script invocation succeeded without reapplying the patch. Existing Editor Vulkan tests still pass 3/3 using the ordinary patched cache. The separately built library was not linked into the Editor, no second Vulkan host was available, and Visual Studio project generation was not repeated because `genie` is absent from the staged cache. Therefore this confirms patch applicability and compilation only, not second-host runtime behavior or portability. Keep Vulkan opt-in and the existing D3D11 default until more hardware evidence exists. See [07.2cu report](reports/07.2cu-bgfx-patch-clean-build.md).

### D234 — Vulkan resize repeatability on one host (07.2cv)

When a second Vulkan machine is unavailable, repeat the fixed-size and grow/shrink tests on the local host for run-to-run stability, but do not treat repetition as cross-driver validation. The three focused tests passed five consecutive times each (15/15) on the one enumerated physical Vulkan device, AMD Radeon Graphics. The 1100×720 tap returned to the expected shrink extent and measured 768/768 matches, maxDelta 0. Keep Vulkan opt-in and D3D11 default until a second host/driver is exercised. No source or dependency changes in this substage. See [07.2cv report](reports/07.2cv-vulkan-resize-repeatability.md).

### D235 — Vulkan two-cycle resize regression smoke (07.2cw)

Where a second host cannot be exercised, add an opt-in same-process stress mode with a bounded transition count and strict captured-extent assertions. The 07.2cw test performs two 1100×720 → 1280×800 → 1100×720 cycles, completes before the existing final readback, and keeps capture output bounded to nine images. It passed five consecutive runs and the full suite recorded 118 passes with one unavailable mixed-DPI skip. This tests repeated state transitions on the existing AMD Vulkan device only; it does not establish portability. Keep Vulkan opt-in and D3D11 default. See [07.2cw report](reports/07.2cw-vulkan-multi-cycle-resize-smoke.md).
### D236 — bounded PointLight3D contract and rendering (07.2cx)

Introduce a separate optional point-light component rather than changing `DirectionalLight3D` semantics. Store enabled state, linear RGB `[0,1]`, intensity `[0,64]`, and finite range `[0.1,10000]` scene units. Resolve the owning Transform origin through the scene hierarchy and publish immutable-by-convention packet entries in stable EntityId order. Scene v13 adds the optional record; v1–v12 remain readable and v12 does not infer a light. The BGFX shader limit is four point lights, with excess entries diagnosed. Use direct Lambert diffuse with `max(1-d/r,0)^2/max(d^2,0.01)` attenuation and clamp accumulated direct diffuse to `[0,1]`. This is a bounded prototype, not a physical/PBR lighting guarantee. The D3D11 smoke verifies visible blue pixels in Scene and Game views; the second Vulkan computer remains unavailable. No Inspector UI, shadows, spotlights, or new dependencies. See [07.2cx report](reports/07.2cx-point-light-scene-game-view.md).

### D237 — PointLight3D Inspector authoring (07.2cy)

Keep the point-light Inspector as a thin editor over the existing command contract: Add, Apply and Remove must execute `light3d.add_point`, `light3d.set_point`, and `light3d.remove_point`, respectively. Retain bounded component validation in Scene commands instead of silently clamping invalid values in the UI. The edit draft is keyed to Scene/entity/component identity and reloads when selection or component identity changes. Localize all user-facing controls. UI smoke checks that controls render; Scene command integration tests remain the source of truth for transaction Undo/Redo, while the point-light render smoke protects visible output. This changes no file schema, public Scene API, renderer contract, or dependency. See [07.2cy report](reports/07.2cy-point-light-inspector.md).

### D238 — DirectionalLight3D Inspector authoring (07.2cz)

Keep directional-light direction derived from the owner's Transform rather than adding a competing direction property. Display the resolved world direction and explain that local `-Z` propagates through parent rotations; users author its orientation through Transform. Add, Apply, and Remove use the existing `light3d.add_directional`, `light3d.set_directional`, and `light3d.remove_directional` transactions. The Inspector draft is keyed by Scene/entity/component identity, reuses Scene's bounded RGB/intensity validation, and is localized. Smoke checks controls and command Undo/Redo while the existing oriented front/back pixel assertions remain unchanged. No schema/API/dependency change. See [07.2cz report](reports/07.2cz-directional-light-inspector.md).

### D239 — pointer-driven light Inspector smoke (07.2d0)

Validate Apply through the real rendered button and typed command callback by injecting ImGui mouse events at the measured center of each light Inspector button. Use a unique fixed-position test window to make the target deterministic; verify the new component values, undo the fixture changes, and restore its clean smoke state. No product API or scene schema change. Evidence is specific to this Windows/MSVC/D3D11 host; broader input/platform coverage remains open. See [07.2d0 report](reports/07.2d0-light-inspector-pointer-smoke.md).

### D240 — light Inspector widget input smoke (07.2d1)

Extend the same pointer harness through the real Enabled checkbox and intensity slider. Dragging across the measured slider track must change the draft; Apply must copy the checkbox and slider values into the component; Undo must restore the fixture. Keep this coverage test-only, without changing command semantics or adding dependencies. RGB coverage is added by D241. See [07.2d1 report](reports/07.2d1-light-inspector-control-input.md).

### D241 — RGB light Inspector pointer input (07.2d2)

Exercise each R, G and B sub-slider independently in both point and directional light Inspector smoke windows. Derive pointer tracks from the live item width and ImGui multi-item spacing, then assert each channel changed in the draft, the typed Apply command copied all channels, and Undo restored the original color. Keep this as test-only coverage: no new product command, schema, or dependency is warranted. Evidence is from one Windows x64/MSVC/D3D11 host. See [07.2d2 report](reports/07.2d2-light-inspector-rgb-pointer-input.md).

### D242 — Editor Inspector keyboard and gamepad accessibility assessment (07.2d3)

Do not infer complete keyboard or gamepad accessibility from ImGui's configured keyboard flag, SDL backend capabilities, or Runtime input-adapter tests. Editor keyboard navigation is configured but lacks focused control-flow tests. Gamepad navigation is not active because SDL gamepad initialization and ImGui's `NavEnableGamepad` flag are absent. Keep this as an explicit implementation/test follow-up: enable the SDL gamepad subsystem with lifecycle-safe ownership, enable ImGui gamepad navigation, then validate keyboard traversal/edit/activation and virtual-gamepad focus/edit/activation plus disconnect behavior in isolated smoke tests. Physical hardware and non-Windows checks remain manual/platform validation. No code behavior changed in the assessment. See [07.2d3 report](reports/07.2d3-inspector-accessibility-assessment.md).

### D243 — synthetic keyboard navigation in light Inspector (07.2d4)

Keep this increment test-only. The point/directional light smoke harness injects Tab and Enter through ImGui's `AddKeyEvent`, verifies keyboard focus reaches Enabled and intensity, verifies the checkbox changes only the Inspector draft, and confirms the Scene component remains unchanged until Apply. Do not report this as end-to-end SDL or physical-keyboard support: the test bypasses SDL event delivery. It also does not validate numeric slider adjustment or keyboard Apply/Undo. Those remain follow-up coverage; gamepad navigation is still inactive. No public API, scene schema, dependency, or product input behavior changes. The Windows x64/MSVC/D3D11 Release run passed 119/120 tests with the mixed-DPI case skipped because a suitable display pair was unavailable. See [07.2d4 report](reports/07.2d4-light-inspector-keyboard-navigation.md).

### D244 — SDL keyboard route for light Inspector (07.2d5)

Use `SDL_PushEvent` with the active Editor window ID for keyboard smoke input, so synthetic key events traverse the same SDL poll and ImGui SDL3 backend route as application events. Verify only the observed contract: Tab focus reaches Enabled/intensity, Enter toggles the Enabled draft, and Scene remains unchanged before Apply. Do not call this physical keyboard validation. Keyboard numeric entry and Apply/Undo did not pass and remain unsupported/unverified by this increment; do not hide that gap by applying with pointer input. No product input API, command, schema, or dependency changes. Windows x64/MSVC/Release/D3D11 focused CTest passed 3/3; full CTest passed 119/120 with mixed-DPI skipped because the required display pair was unavailable. See [07.2d5 report](reports/07.2d5-light-inspector-sdl-keyboard.md).
### D245 — Light Inspector keyboard edit diagnostic (07.2d6)

Keep the keyboard numeric-field gap explicit. On Windows x64/MSVC/Release/D3D11, smoke and normal interactive Editor checks both reach the intensity slider, but Enter, Right and text entry leave its draft unchanged. The smoke reports `WantTextInput=1`, visible navigation cursor and SDL backend text-event acceptance; the interactive check uses Windows UI keyboard injection rather than the smoke event queue. This reproduces a user-facing gap in the tested build, but does not isolate whether the responsible layer is control focus/activation, ImGui or Inspector behavior. A trial setting `ConfigNavCursorVisibleAlways` globally changed pointer-smoke behavior and was rejected. Do not attempt keyboard Apply/Undo until a keyboard value edit succeeds. Full Release CTest passed 119/120 with the mixed-DPI case skipped because a suitable display pair was unavailable. Physical keyboard hardware remains untested. Next implement a narrowly scoped fix and regression coverage. See [07.2d6 report](reports/07.2d6-light-inspector-keyboard-edit.md).

### D246 — Numeric light Inspector draft input (07.2d7)

Keep the existing intensity slider and typed Apply command, and add a localized numeric `InputFloat` for PointLight3D and DirectionalLight3D. SDL smoke now enters `5.25` through the text input path, verifies the Inspector draft changes, and verifies the Scene stays unchanged before Apply. The two focused light smokes and localization unit test pass. Apply/Undo continue to be covered by the existing pointer and command-history smoke; keyboard activation of Apply and keyboard-only Apply/Undo are not claimed by this substage. No Scene schema, public command API, renderer, or dependency changed. Full Release CTest ran but had two Vulkan resize-capture failures; one passed on focused rerun while resize-stress reproduced. See [07.2d7 report](reports/07.2d7-light-inspector-numeric-keyboard-edit.md).

### D247 — Capture Vulkan resize baseline before queuing transitions (07.2d8)

Request the baseline screenshot well before the first synthetic resize transition. BGFX services screen-shot requests on its render thread after submitted frames, so a request made on frame 19 can race a resize scheduled at frame 20 and capture the new extent even though the Editor has not yet logged the resize event. Move only the trace baseline request to frame 8; retain the exact screenshot count, transition sequence, dimensions, and readback assertions. Windows x64/AMD Vulkan focused resize checks passed **3/3**, the two-cycle stress test passed **5/5**, and full Release CTest passed **119 with one unavailable mixed-DPI skip of 120**. This is one-host test-harness timing evidence, not cross-driver validation. See [07.2d8 report](reports/07.2d8-vulkan-resize-baseline-capture.md).

### D248 — In-process Codex project context and plan validation (09.2)

- Keep the first Codex adapter in a separate optional `astro_ai_editor` module (off by default; included by tests when enabled) that depends on the existing Scene and Command API; do not add a network service or third-party protocol dependency.
- Expose a deterministic read-only copy of the active in-memory Scene and the commands actually registered in `CommandRegistry`.
- Accept only descriptive plans that refer to registered command IDs and current schema versions. Do not execute plans or imply that metadata-only validation checks typed payloads.
- Do not scan project directories in this substage. This avoids granting filesystem access before a project-root allowlist and preview/approval design exist.
- The adapter remains trusted in-process code, not a security boundary. JSON/IPC, full project tree, payload validation, diff, permissions, approval, and execution remain future work. See [AI_INTEGRATION.md](AI_INTEGRATION.md) and [09.2 report](reports/09.2.md).

### D249 — Read-only property diff as first 09.3 slice

- Start preview coverage with existing transactional `entity.set_property` v1 and expose typed before/after values instead of lossy formatted strings.
- Validate the requested value against the supplied Scene's actual property schema and confirm that the matching command version is registered. Do not mutate the Scene or create an approval token in this slice.
- Treat preview as a snapshot only; any later apply must revalidate against current state to prevent stale-preview application.
- Keep Editor consent, broader command previews, permissions, checkpoints, and rollback as separate follow-up work. A boolean from an in-process caller is not direct user confirmation. See [09.3a report](reports/09.3a-property-preview.md).

### D250 — Direct Editor confirmation for property review (09.3b)

- Use a visible Editor confirmation action for the first applied AI proposal; do not accept an approval boolean from the adapter.
- Limit this slice to entity-name edits through the existing typed `entity.set_property` command. The preview is read-only; confirmation rechecks project/scene identity and the current typed before/after values before calling `AIEditorApi`, preserving normal validation and Undo/Redo.
- Treat the review model as in-process UI flow only. It does not grant permissions, isolate untrusted callers, implement JSON/IPC, or provide general checkpoints/rollback. Broader command payloads remain future work.
- Unit and window smoke coverage plus full Release CTest results are recorded in [09.3b report](reports/09.3b-editor-confirm-apply.md) and [TESTING](TESTING.md).

### D251 — Extend direct review to existing SpriteRenderer visibility (09.3c)

- Add review for the already-registered transactional `entity.set_sprite_renderer` command, limited to visibility changes on an existing component.
- Preserve the component's current AssetId and component identity; revalidate component ID, AssetId, prior visibility, project/scene identity, and Scene ID before apply. Do not add/remove the component or choose a new asset in this review surface.
- Delegate apply and Undo/Redo to the existing command. Keep the adapter/review flow in-process and trusted; permissions, sandbox, external transport, and general rollback remain future work.
- Coverage and results are recorded in [09.3c report](reports/09.3c-sprite-visibility-review.md) and [TESTING](TESTING.md).

### D252 — AI-reviewed creation through the existing primitive transaction (09.3d)

- Limit this review operation to the registered `entity.create_primitive` v1 command and the built-in Cube, Plane, Sphere and Cylinder shapes. Expose name, optional selected parent and visibility in the Editor panel.
- Keep Preview read-only. Capture project/scene identity, Scene ID, predicted next Entity ID and the exact typed payload; at direct confirmation, recompute and compare these values before calling `AIEditorApi`.
- Return and select the created Entity ID in normal Editor use. Keep Undo/Redo and serialization in the existing command/Scene paths. Do not create or rewrite files under project Assets.
- The review model is an in-process consent UX for this operation, not a security boundary, permission model, general command approval layer, or remote AI transport. Tests and results are documented in [09.3d report](reports/09.3d-primitive-creation-review.md).

### D253 — AI-reviewed Camera3D addition through the existing transaction (09.3e)

- Limit the operation to adding the default perspective `Camera3D` to the selected existing entity via registered `camera3d.add` v1. Do not add a second command or mutate Scene from the review panel.
- Keep Preview read-only. Before confirmation, recheck project/scene identity, Scene ID and that the exact target still exists and has no conflicting camera. Let the existing transactional command own validation, undo/redo and component identity.
- New cameras are inactive. Adding a camera must not silently switch the Game View or alter the current active camera selection.
- Treat this as a narrow in-process confirmation workflow, not general AI permissions or a security boundary. Full review for arbitrary commands, external transport and sandboxing remain separate work. Results are recorded in [09.3e report](reports/09.3e-camera-addition-review.md) and [TESTING](TESTING.md).

### D254 — AI-reviewed Camera3D lens changes through the existing transaction (09.3f)

- Limit this operation to FOV, near-plane and far-plane values of an existing Camera3D using registered transactional `camera3d.set_lens` v1. Do not change active-camera selection in this operation.
- Keep Preview read-only. Snapshot the target entity and Camera3D Component ID, Scene ID, project/scene identity, exact before/after lens values and active state. Confirmation must reject any change to that snapshot and must not apply a no-op.
- Delegate mutation, validation and Undo/Redo to the existing command. Preserve Component ID and active flag; explicitly disclose in the diff when the target camera is active because its accepted lens values affect Game View.
- This remains a narrow in-process consent workflow, not a general permission boundary or remote executor. Coverage and results are documented in [09.3f report](reports/09.3f-camera-lens-review.md) and [TESTING](TESTING.md).

### D255 — AI-reviewed Camera3D selection through the existing transaction (09.3g)

- Represent activation and clearing as distinct explicit review actions over registered transactional `camera3d.set_active` v1; payload entity `0` means clear. Do not add an alternate Scene mutation path.
- Preview records Scene ID, current active entity/component, target entity/component, and whether the selection changes. Confirmation revalidates project/scene identity and the complete active/target identity snapshot; reject stale state and no-op changes before command execution.
- Delegate validation and Undo/Redo to `camera3d.set_active`. Disclose the visible impact. Clearing affects the edit Scene and its Edit Mode Game View; Play Mode keeps its independent runtime Scene.
- This remains a narrow in-process review UX, not a security boundary, general command permission system, or remote executor. Evidence is recorded in [09.3g report](reports/09.3g-camera-selection-review.md) and [TESTING](TESTING.md).

### D256 — Transform review requires a typed transactional command (09.3h assessment)

- Do not mutate `Scene::setTransform` directly from AI review. No registered typed transactional Transform command exists today; `entity.set_property` only handles the entity name.
- Treat the persisted Transform as local to its parent. Treat hierarchy-composed world matrix and Camera3D pose as derived read-only values. A preview must label both spaces and must never interpret a local payload as a world-space destination.
- Before a later AI review can safely apply Transform, provide an explicit typed command with finite/range validation, transaction audit and Undo/Redo. Camera world-pose output is recomputed from the parent chain; any intervening ancestor change makes the preview stale.
- 09.3h is assessment-only: no Scene/API/editor source behavior or dependencies changed. Baseline and results are recorded in [09.3h report](reports/09.3h-transform-contract-assessment.md) and [TESTING](TESTING.md).

### D257 — Local Transform changes use a full typed transaction (09.3i)

- Register `entity.set_transform` v1 as a transactional Scene command with one full local Transform payload. Publish position, Euler degrees and scale as nine numeric schema fields plus the stable EntityId; world-space values are not accepted by this command.
- Validate on a Scene copy through the existing `Scene::setTransform` contract. Preserve compatibility by requiring finite values without inventing new numeric bounds or rejecting zero scale in this command. Consumers may reject singular world matrices with their existing diagnostics.
- Capture the full prior local value and Transform ComponentId. Apply/Undo/Redo must route through `Scene::setTransform` and verify the component identity before modifying it.
- This command does not imply a new AI preview UI or a world-space command. These remain separate follow-up work. Focused and full regression evidence is recorded in [09.3i report](reports/09.3i-transform-command.md) and [TESTING](TESTING.md).

### D258 — AI review for local Transform (09.3j)

- Expose the existing `entity.set_transform` v1 command through AI Change Review using an explicitly local Transform payload; do not reinterpret it as a world-space destination.
- Snapshot the target Scene/Entity/Transform Component identity and the local Transform plus parent links, Transform IDs and values from the entity through the root. Recompute before confirmation and reject any stale chain, so a displayed derived Camera3D pose cannot silently diverge.
- Compute world Camera3D position, target and up on a Scene copy after substituting the proposed local Transform. Keep this output read-only and separate from the command diff; if resolution fails, report unavailable and retain the local preview.
- Keep the existing finite-only validation, including zero scale, and use the registered transaction for apply/Undo/Redo. The review UX remains in-process and is not a security boundary. See [09.3j report](reports/09.3j-transform-review.md) and [TESTING](TESTING.md).

### D259 — transactional hierarchy reparent and AI review (09.3k)

- Define parent changes as **preserve local Transform**. `entity.reparent` v1 changes the parent link only; descendants stay attached and world poses are derived from the resulting hierarchy. A mode that preserves world pose is not included because transform decomposition under rotated/non-uniformly scaled ancestors needs a separate explicit contract.
- Represent the root as `parent = 0` in the typed command schema. Validate proposed hierarchy on a Scene copy and perform apply/Undo/Redo through a transaction that checks the current parent and stable Transform ComponentId.
- Before AI review confirmation, recompute and compare project/scene identity, source hierarchy, destination hierarchy, target local Transform/ComponentId and the entire affected subtree. Reject stale/no-op proposals and direct users to prepare a fresh review.
- Display affected subtree size and derived Camera3D world pose when available. Keep preview read-only and the confirmation flow in-process; it is not a permission or security boundary. See [09.3k report](reports/09.3k-entity-reparent-review.md), [Command API](COMMAND_API.md), and [TESTING](TESTING.md).


### D260 — Hierarchy drag/drop uses the reparent transaction (09.3l)

- A single entity row dragged onto another entity changes its parent through the existing registered `entity.reparent` v1 command. Dropping on the explicit Scene-root target sends `parent = 0` through the same command. Do not modify Scene hierarchy from the widget directly.
- Preserve the command's local-Transform semantics, stable Transform ComponentId, descendants, validation and Undo/Redo. Mark the Scene dirty after a successful transaction so normal save/reload handles persistence.
- Keep this first interaction single-entity. Multi-select drag and world-pose-preserving behavior require separate explicit design and tests. See [09.3l report](reports/09.3l-hierarchy-drag-reparent.md) and [TESTING](TESTING.md#подэтап-093l--hierarchy-dragdrop-reparent).


### D261 — оценка контракта группового переподчинения (09.3m; рекомендация, не утверждённый API)

- Сохранять `entity.reparent` v1 как одиночную команду. Для будущего группового действия применять один typed payload и одну запись истории Undo/Redo; не вызывать v1 циклом из UI, так как это создаёт частичную группу и отдельные Undo.
- UI-кандидат: обычный клик заменяет выбор, Ctrl+Click toggles элемент; перетаскивание уже выбранной строки перемещает набор, иначе сначала выбирается только строка-источник. Shift-range selection отложить.
- Нормализовать выбор до верхних выбранных корней. Отбрасывать повторяющиеся/несуществующие ID, отклонять пустой набор, межсценовую цель и цель, совпадающую с любым корнем или лежащую в его поддереве. Если все корни уже имеют выбранного родителя, не создавать запись истории.
- Валидация полного набора должна закончиться до мутации. Применение всех изменённых корней — единый `CommandTransaction`; сохранять parent, Transform ComponentId и local Transform для stale checks/Undo. Ошибка шага должна откатывать ранее применённые шаги. Сохранять исходный порядок сущностей Scene; отдельное sibling-order API отсутствует.
- Не передавать `uint64_t EntityId` как floating-point `NumberArray`. Перед реализацией выбрать и протестировать unsigned-integer array schema либо зафиксировать typed-only boundary. Предложение 09.3m — assessment, не разрешение реализации; см. [report](reports/09.3m-multi-reparent-assessment.md).

### D262 — единая typed-транзакция группового переподчинения (09.3n)

- Сохранить `entity.reparent` v1 для одиночной операции; добавить `entity.reparent_many` v1 с `entities: UnsignedIntegerArray` и `parent: UnsignedInteger` (0 означает корень). Не представлять EntityId через `NumberArray`.
- Нормализовать список до верхних выбранных корней, удаляя дубликаты и выбранных потомков выбранного предка. Пустой набор, отсутствующая сущность, отсутствующий Transform, неизвестная цель и цикл отклоняются до изменения live Scene.
- Все отличающиеся parent links применять одним `CommandTransaction`, чтобы Undo/Redo создавали одну запись на группу. No-op не добавляет запись. Сохранять локальные Transform и ComponentId; не менять порядок сущностей Scene.
- В Editor Ctrl+Click переключает членство; обычный клик по выбранной строке сохраняет текущую группу до начала drag, но без drag схлопывает выбор до этой строки. Перетаскивать можно только выделенную строку; её payload содержит нормализованные выбранные корни.
- Это локальная UX/Command-функция, не новая AI authorization boundary. World-pose preservation, Shift-range selection и group Inspector остаются за пределами данного контракта. Проверки и ограничения: [09.3n report](reports/09.3n-group-reparent.md).

### D263 — рекомендуемый контракт массового Inspector (09.3o assessment)

- Первый срез массового Inspector ограничить обязательным `Transform` всех выбранных сущностей; не выводить редактирование разнородных компонентов, assets, материалов или скриптов до отдельной модели совместимости.
- Подписывать значения как локальные относительно каждого родителя. Для каждого поля показывать mixed-state, если значения различаются. Изменение выбранного поля должно сохранять остальные локальные координаты/повороты/масштабы каждого объекта.
- Применять группу одной новой typed transactional-командой над структурированным списком `{EntityId, Transform}`. Не вызывать `entity.set_transform` циклом и не сплющивать ID+Transform в неоднозначный `NumberArray`. Вся группа валидируется до записи; Undo/Redo атомарны; команда сверяет Scene/entity/Transform ComponentId/value snapshot перед применением.
- До реализации определить schema/catalog representation для structured array и UX для stale selection/draft. Текущий `CommandSchema` не предоставляет вложенный объектный тип, а внешнего JSON transport нет. Это решение пока рекомендация; implementation authorization для следующего подэтапа не заявляется. Подробнее: [09.3o](reports/09.3o-multi-inspector-assessment.md).

### D264 — групповой Inspector ограничен local Transform (09.3p)

- Общий Inspector редактирует только девять полей обязательного Transform. Mixed values показываются для каждого поля независимо; изменённое поле одинаково применяется ко всем выбранным объектам, остальные локальные поля сохраняются на объект.
- Значения и UI явно local-to-parent. Иные component inspectors остаются привязаны к primary entity до отдельного контракта совместимости.
- `entity.set_transforms` v1 использует `TransformAssignmentArray` с typed `{EntityId, full local Transform}` entries; не сериализовать ID через `NumberArray`. Вся группа валидируется до мутации, no-op пропускается, изменения образуют одну transactional history entry. Apply/Undo/Redo сверяют ComponentId и ожидаемые значения.
- Inspector draft содержит snapshot и блокирует Apply, если Scene/Transform изменились; смена выбора сбрасывает draft с сообщением. Успех помечает Scene dirty и использует обычное сохранение.
- Этот каталоговый тип пока только typed C++ contract: внешний JSON/remote codec не реализован. Групповое редактирование компонентов, материалов и скриптов не входит в решение. Проверки, ограничения и результаты — в [09.3p](reports/09.3p-multi-transform-inspector.md).

### D265 — клавиатурный контракт и защита stale draft группового Inspector (09.3q)

- Smoke должен вводить значение через тот же SDL text-event и ImGui `InputDouble`, которыми пользуется поле редактора, а не подменять черновик напрямую. Enter фиксирует значение; до команды Apply live Scene не меняется.
- Если текущая Scene/Transform больше не совпадает со snapshot черновика, UI сохраняет черновик для явного решения пользователя, помечает его устаревшим и отключает Apply. Reload обновляет snapshot и очищает черновик.
- Решение не меняет публичный Command API или схему сцены. Проверенный путь автоматизирует ввод на уровне SDL/ImGui; наличие и поведение физической клавиатуры вручную не подтверждались. Результаты: [09.3q](reports/09.3q-multi-transform-keyboard-stale-draft.md).

### D266 — подтверждение сброса draft при смене multi-selection (09.3r)

- Групповой Transform Inspector при изменении набора выбранных Entity сбрасывает все unapplied overrides и показывает предупреждение. Новый snapshot строится из текущего выбора; старые значения не переносятся автоматически на другую группу.
- Smoke меняет контролируемое выделение с двух сущностей на одну при активном draft, затем проверяет сброс и вызов UI сообщения.
- Решение не меняет команду `entity.set_transforms`, формат Scene или контракт Undo/Redo. Детали и проверки: [09.3r](reports/09.3r-multi-transform-selection-draft-discard.md).
