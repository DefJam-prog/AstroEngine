# Этапы, подэтапы и критерии приёмки

### 07.2d6 — диагностика клавиатурного редактирования светового Inspector (завершён с ограничениями)

SDL smoke и обычное окно Editor подтверждают проблему: фокус доходит до intensity, но Enter, Right и ввод текста не меняют draft; клавиатурные Apply/Undo не проверялись. Windows UI keyboard injection воспроизводит проблему вне тестового smoke; физическое устройство не проверялось. Глобальная настройка постоянно видимого курсора отклонена из-за регрессии pointer smoke. Причина внутри UI-пути ещё не локализована. Следующий подэтап — узко исправить активацию/редактирование и покрыть регрессией; ручная клавиатура остаётся отдельной платформенной проверкой. См. [D245](DECISIONS.md#d245--light-inspector-keyboard-edit-diagnostic-072d6) и [отчёт](reports/07.2d6-light-inspector-keyboard-edit.md).

### 07.2d5 — SDL keyboard route светового Inspector (завершён с ограничениями)

Tab/Enter smoke events идут через `SDL_PushEvent` → SDL poll → `ImGui_ImplSDL3_ProcessEvent`; ImGui focus достигает Enabled/intensity, Enter меняет локальный draft-флажок, Scene остаётся неизменной до Apply. Это подтверждает синтетический SDL event route, но не физическую клавиатуру. Ввод числа в slider и клавиатурные Apply/Undo не прошли smoke и остаются незавершёнными. Полный Release CTest: **119 passed, 0 failed, 1 skipped из 120**. См. [D244](DECISIONS.md#d244--sdl-keyboard-route-for-light-inspector-072d5) и [отчёт](reports/07.2d5-light-inspector-sdl-keyboard.md).

### 07.2d4 — синтетическая клавиатурная навигация светового Inspector (завершён с ограничениями)

Точечные smoke-тесты Point/Directional Light теперь передают Tab/Enter на вход ImGui, проверяют фокус Enabled и intensity, переключение локального draft-флажка и неизменность Scene до Apply. Это проверяет ImGui navigation/draft, но не SDL/физическую клавиатуру, числовое редактирование или клавиатурную активацию Apply; эти сценарии требуют отдельного этапа. Editor gamepad navigation по-прежнему выключена. Полный Release CTest: **119 passed, 0 failed, 1 skipped из 120**; mixed-DPI тест пропущен без пары дисплеев с разным масштабом. См. [D243](DECISIONS.md#d243--synthetic-keyboard-navigation-in-light-inspector-072d4) и [отчёт](reports/07.2d4-light-inspector-keyboard-navigation.md).

### 07.2d3 — оценка доступности Inspector с клавиатуры и геймпада (оценка завершена)

Клавиатурная навигация ImGui включена, но отдельного smoke-теста перехода по контролам и их изменения клавиатурой нет. Навигация Editor геймпадом не активирована: приложение инициализирует только SDL Video и не включает `ImGuiConfigFlags_NavEnableGamepad`, хотя SDL3 backend содержит опрос геймпада. Отдельный этап реализации должен включить корректный SDL lifecycle и тестировать навигацию виртуальным SDL-геймпадом; физический контроллер и другие ОС остаются отдельной ручной проверкой. См. [D242](DECISIONS.md#d242--editor-inspector-keyboard-and-gamepad-accessibility-assessment-072d3) и [отчёт](reports/07.2d3-inspector-accessibility-assessment.md).

### 07.2d2 — RGB input for light Inspector (implemented)

Point and directional light smoke tests now independently drag the R, G and B controls by pointer, apply the edited color through the existing typed command and undo it. Assertions cover all three draft channels, the committed component values and restoration to the original color. Focused Release tests passed **3/3**; full Release CTest passed **119 tests, 0 failed, 1 skipped of 120**. The mixed-DPI test was unavailable on this host. See [D241](DECISIONS.md#d241--rgb-light-inspector-pointer-input-072d2) and [report](reports/07.2d2-light-inspector-rgb-pointer-input.md).

### 07.2d0 — pointer-driven light Inspector smoke (implemented)

Point and directional light Inspector Apply buttons are now activated by injected ImGui mouse down/up events using each rendered button's measured position. The smoke verifies both command effects and Undo restoration, while existing visual lighting checks remain active. Focused Release tests passed **3/3**; full Release CTest passed **119/120**, with the mixed-DPI test skipped because no suitable monitor pair was available. Next recommended substage: exercise edits to light controls themselves (checkbox and slider drag), with equivalent state and Undo checks. See [D239](DECISIONS.md#d239--pointer-driven-light-inspector-smoke-072d0) and [report](reports/07.2d0-light-inspector-pointer-smoke.md).

### 07.2d1 — light Inspector checkbox and intensity drag (implemented)

The point and directional light smokes toggle Enabled, drag the intensity slider, and exercise every RGB channel via pointer input. Draft changes are applied through existing typed commands and undone to the original component state. RGB follow-up is completed in 07.2d2. See [D240](DECISIONS.md#d240--light-inspector-widget-input-smoke-072d1) and [report](reports/07.2d1-light-inspector-control-input.md).

### 07.2ce — capability-gated sRGB targets and linear-light blending (implemented prototype)

On Windows x64 with BGFX D3D11, Scene/Game BGRA8 targets (view IDs 1 and 2) now use mutable sRGB color attachments only when BGFX advertises both sRGB texture and framebuffer support. Sprite texture/tint producers use linear RGB for those targets; mesh shaders leave output linear for hardware encoding/blending. The Editor swapchain remains unchanged and ImGui samples the encoded attachment without requesting sRGB decode. Unsupported backends keep the legacy output path with a diagnostic. No scene/material schema or authored asset changed. Production-shader GPU smoke measured RGB 93/66/128/66 and a 0.5-alpha linear-byte-55 blend at RGB 92 (14,336 pixels each, ±3 tolerance). Alpha/MASK/mipmap screenshot classifiers were updated to measured sRGB-target output. A normal full Release CTest run had one intermittent `editor.asset_model_preview.smoke` synthetic-drag capture failure (109 passed, 1 skipped, 1 failed of 111); isolated reruns passed. A full retry run using `--repeat until-pass:3` passed **110 tests with 1 skip and 0 final failures** in 318.11 s. Mixed-DPI test was skipped without a monitor pair. Limits: D3D11 only, no full viewport-crop numeric equivalence to Editor UI, no other backend and no PBR claim. Details: [report](reports/07.2ce-linear-light-srgb-attachments.md), [D217](DECISIONS.md#d217--capability-gated-srgb-scene-targets-072ce).

### 07.2cd — оценка alpha blending и Scene/Game composition (assessment complete)

Подтверждена причина gamma-space BLEND: 3D shader кодирует RGB в sRGB до straight-alpha blending в обычный BGRA8 target; эталонный sRGB 128 × alpha 0,5 поверх чёрного даст около 64 вместо около 92 при линейном смешивании и последующем sRGB encoding. Viewport имеет непрозрачный clear и показывается pass-through ImGui, поэтому его изображение копируется без цветовой коррекции. Кандидат для проверки — capability-gated mutable-sRGB BGRA8 attachment; перед переводом target нужно линеаризовать также 2D sprite texture/tint path. Runtime не менялся. План теста и backend ограничения — [оценка 07.2cd](reports/07.2cd-alpha-blend-target-assessment.md).

Historical recommendation from 07.2ce; completed by 07.2cf below.

### 07.2cc — согласование BGFX color transfer и numeric screenshot smoke (implemented)

BGFX Scene/Game shaders now treat uploaded base-color texture samples as linear BGRA8: no second sRGB decode, `baseColorFactor` is applied once, and unlit texture/vertex-color shaders encode output RGB once for ordinary BGRA8. Added a Windows x64/D3D11 screenshot test over the production fragment shaders for mid-gray/non-white factor and all four texture/vertex-color × lit/unlit paths. Exact measured RGB: **93, 66, 128, 66**; tolerance is ±3, 14,336 matching pixels per panel. Existing alpha/MASK/mip classifiers were updated to their measured encoded screenshot ranges; the renderer API and file schemas did not change. Full Release CTest: **110 passed, 0 failed, 1 skipped of 111**. See [D215](DECISIONS.md#d215--bgfx-base-color-transfer-owner-and-output-encoding-072cc), [TESTING](TESTING.md#072cc-bgfx-color-transfer), and [report](reports/07.2cc-bgfx-color-transfer.md).

Historical recommendation from 07.2cc, completed by the 07.2cd assessment above.

### 07.2cb — оценка контракта цветового пространства (assessment complete)

Решение: сохранить семантику цвета по роли ресурса и материала; не добавлять общий serialized `ColorSpace` в `.astro-material`. Для glTF `baseColorFactor` и `COLOR_0` — linear multipliers, base-color RGB texture encoded sRGB, alpha linear. Однако текущие optional BGFX Scene/Game View цепочки не реализуют эти данные согласованно: CPU конвертирует base-color bytes в linear BGRA8 и применяет factor до upload, а lit textured shader повторно декодирует sample как sRGB. Shader output target также требует явного решения: offscreen targets — обычный BGRA8, а smoke не проверяет end-to-end transfer function на средних цветах. Это документальная оценка; runtime поведение не изменено. См. [D214](DECISIONS.md#d214--material-color-semantics-vs-renderer-transfer-contract-072cb), [TESTING](TESTING.md#072cb-material-color-space-contract-assessment), [report](reports/07.2cb-material-color-space-contract.md).

Historical recommendation from 07.2cb, completed by 07.2cc: reconcile the BGFX base-color transfer path and add numeric GPU screenshot checks. See the implementation and current limits above.

### 07.2ca — directional-light image equivalence and face regions (implemented prototype)

The Scene View target now receives the existing Lambert shader handles and uniforms. Its isolated smoke uses identical two-plane scenes with a red directional light, no-light legacy shader, or the new Lambert shader with zero active lights. The captured Scene View raw crops match byte-for-byte (SHA-256 `486a34701d3ad1cf3216a5b134d3074455e6efccb3d2d5ccc00995692435cd92`). In the lit capture the controlled front region had 6,724 red pixels and the back-facing region had 0; Game View continues to report 2,304 red pixels. The fixture is deliberately separate from the broader editor smoke scene so other sprites/materials cannot satisfy the pixel checks. Scene View shader color handling is documented as an implementation detail, not a cross-backend contract. Windows x64/D3D11 only; no PBR, shadows, or other GPU backend validation. See [D213](DECISIONS.md#d213--scene-view-lighting-equivalence-and-face-smoke-072ca), [TESTING](TESTING.md#072ca-scene-view-lighting-equivalence), and [report](reports/07.2ca-scene-view-lighting-equivalence.md).

### 07.2bz — bounded directional diffuse in Scene View (implemented prototype)

Scene View uploads authored mesh normals, resolves enabled `DirectionalLight3D` world directions, and renders up to four stable-EntityId directional lights with the project-owned Lambert shaders and inverse-transpose world normal transform. The lit shader path is wired to the Scene View target; without enabled lights, the legacy unlit programs remain selected. Project model preview remains unlit. Stage 07.2ca verifies exact zero-light output equivalence and front/back pixel regions. Historical implementation details: [D212](DECISIONS.md#d212--scene-view-directional-diffuse-072bz) and [report](reports/07.2bz-scene-view-directional-diffuse.md).

07.2ca closes the original no-light image and face-region assertions. Its material color-space implementation was assessed in 07.2cb.

### 07.2by — bounded BGFX directional diffuse in Game View (implemented)

Runtime BGFX Game View submits normals, transforms them by inverse-transpose world matrices, and evaluates up to four stable-order directional lights with project-owned shaders. The shader code attempts sRGB decode/linear lighting/sRGB encode, At 07.2cb a transfer mismatch was found for base-color textures; 07.2cc corrected and numerically checked it. No-light scenes retain the old shader output. Missing-normal geometry remains visible unlit with a diagnostic, and lights beyond the four-light shader budget are diagnosed and omitted. The Windows D3D11 smoke verifies light-colored pixel presence, not numeric color fidelity. See [D211](DECISIONS.md#d211--bounded-bgfx-directional-diffuse-game-view-072by), [report](reports/07.2by-bgfx-directional-diffuse.md), and [07.2cb assessment](reports/07.2cb-material-color-space-contract.md).

Historical recommendation from 07.2by: connect the light contract to Scene View and strengthen pixel comparisons. Scene View lighting was connected in 07.2bz; exact image/face comparisons remain open. Do not infer broad lighting support from this Game View slice.

### 07.2bx — BGFX directional-light visual slice assessment (assessment complete)

Reviewed the current Scene/Game View shaders and Runtime BGFX packet consumer. The adapter ignores `SceneRenderPacket::directionalLights` and does not upload vertex normals; its current fragment shaders output only unlit base color. Recommended implementation is a narrow Lambert diffuse path with correct inverse-transpose normal transforms, a fixed shader light limit and overflow diagnostics, linear-space arithmetic, and explicit final output encoding. Keep missing-normal geometry visibly unlit with a diagnostic. The Editor Game View smoke should compare no-light output with the prior unlit path and verify lit-facing versus back-facing pixel regions, then run existing alpha/texture/fallback regressions. Windows x64 D3D11 only; no additional dependency. Current baseline build is no-op; last full Release CTest from 07.2bw was **108 passed, 1 skipped of 109**. [Assessment](reports/07.2bx-bgfx-lighting-assessment.md).

### 07.2bv — оценка первого lighting vertical slice (assessment complete)

Проверены существующие normal/import/render-packet/Scene/shader границы. NORMAL уже импортируется и валидируется; built-in primitives имеют normals, но `SceneRenderPacket` не включает normal matrix/light data, Scene не имеет Light component, текущие BGFX shaders unlit. Рекомендуемая реализация: versioned DirectionalLight3D Scene component и backend-neutral resolved light data, v11 compatibility, stable world direction independent of scale, tests transform/serialization/command/render packet. При отсутствии активного света сохранять прежний unlit appearance; не включать PBR BRDF/shadows в этот первый vertical slice. Baseline Release build no-op; последний полный Release CTest из 07.2bu — **108 passed, 1 skipped из 109**. [Оценка](reports/07.2bv-lighting-vertical-slice-assessment.md).

### 07.2bu — MASK base-color в Runtime BGFX Game View (implemented prototype)

Runtime adapter принимает OPAQUE и MASK, передаёт alpha cutoff через существующий shader uniform и строит MASK mip chain с учётом coverage. Добавлен Editor Play smoke с прозрачной checker-текстурой; результат: оба mesh/textured submissions прошли через BGFX, fallback=0, source-color crop=83 px. Сохранены OPAQUE/mipmap и BLEND fallback проверки. Full Windows x64/D3D11 Release CTest: **108 passed, 1 skipped из 109 за 156,52 с**; mixed-DPI skipped без подходящей пары дисплеев. BLEND, прочие PBR maps и UV transforms остаются вне adapter. [Отчёт](reports/07.2bu-runtime-bgfx-mask.md).

### 07.2bt — сравнительные smoke mip min-фильтров Runtime BGFX (implemented prototype)

Добавлены четыре независимых Game View smoke для glTF sampler enum 9984–9987 на фиксированной checker-текстуре, уменьшенной в сцене. Проверяются enum, cache/submission и диапазон видимых пикселей. Все четыре проходят на Windows x64/D3D11. Первый полный CTest поймал один сбой существующего `editor.asset_model_preview.smoke`, но тест прошёл в 10 повторах и в следующем полном прогоне: итог **107 passed, 1 skipped из 108 за 171,24 с**. Сбой был transient и не потребовал кода; mixed-DPI skipped без подходящей пары дисплеев. Pixel-count диапазоны не универсальны между драйверами. [Отчёт и диагностика](reports/07.2bt-runtime-bgfx-mipmap-comparison.md).

### 07.2bs — mipmapped opaque base-color в Runtime BGFX (implemented prototype)

Runtime BGFX теперь переиспользует bounded linear-space CPU mip chain, выделяет mip-enabled texture, передаёт каждый уровень через `updateTexture2D`, выбирает BGFX min/mip point flags по общему filter policy и учитывает все уровни в 256 MiB cache budget. Editor Play smoke проверяет внешний glTF source texture с `LINEAR_MIPMAP_LINEAR`, отдельную mip cache entry, source/project texture colors и полное сохранение fallback. Windows x64/D3D11 Release full CTest: **103 passed, 1 skipped из 104 за 166,40 с**. Ограничение: per-level upload API не возвращает backend result; screenshot smoke проверяет один mip mode, а не сравнительный golden всех четырёх. [Отчёт](reports/07.2bs-runtime-bgfx-mipmap.md).

### 07.2br — оценка mip-фильтрации Runtime BGFX (assessment complete)

Runtime BGFX сейчас загружает только уровень 0; существующий `prepareLinearBaseColorMipChainBgra8` способен подготовить bounded linear-space chain с alpha-aware режимами, а редакторские проверки охватывают четыре mip filter modes. Реализация возможна без новой зависимости, но требует проверки BGFX version API/capabilities, per-level upload/lifetime cleanup, подсчёта всех уровней в 256 MiB cache budget и визуального smoke minification. Рекомендованный следующий подэтап — 07.2bs, только opaque base-color с mip; MASK/BLEND и прочие PBR/UV пути пока остаются fallback. Baseline Release build no-op; выбранные тесты **7/7 passed**. [Оценка](reports/07.2br-runtime-bgfx-mipmap-assessment.md).

### 07.2bq — внешние glTF base-color изображения в BGFX Game View (implemented prototype)

Render packet сохраняет безопасный путь внешнего glTF image, его проверенный import SHA-256, UV channel, sampler и UV transform. При отсутствии project material override optional Runtime BGFX может прочитать изображение только под `Assets` через host WIC resolver; cache ключ включает путь, SHA и base-color factor. Поддерживается тот же узкий subset 07.2bp: opaque, UV0, identity transform, nearest/linear non-mip фильтры и базовые wrap modes. Любое неподдержанное или отсутствующее изображение отклоняет весь Runtime pass, сохраняя Editor renderer. Smoke одновременно подтверждает project и source glTF texture submissions; integration test проверяет перенос metadata в packet. Windows x64/D3D11 Release: focused **5/5**, full **103 passed, 1 skipped из 104 за 141,61 с**. [Отчёт](reports/07.2bq-runtime-bgfx-gltf-source-texture.md).

### 07.2bp — opaque base-color texture в BGFX Game View (implemented prototype)

Runtime adapter теперь может декодировать зарегистрированный project base-color image через host resolver, перевести BGRA8 sRGB в linear с baseColorFactor и отрисовать его в Game View с ограниченными wrap/filter настройками. Cache ограничен 256 MiB и keyed по AssetId + SHA-256 + factor. В BGFX order Game View clear предшествует 3D submission, поэтому renderer output остаётся виден при композитинге ImGui. Новая визуальная smoke проверяет 4050 base-color pixels в текущем Windows D3D11 Release прогоне. Focused CTest **7/7 за 14,86 с**, full Release CTest **103 passed, 1 skipped из 104 за 135,00 с**; mixed-DPI skip ожидаем без пары подходящих дисплеев. [Отчёт](reports/07.2bp-runtime-bgfx-textured-game-view.md), [screenshot](reports/07.2bp-runtime-bgfx-textured-game-view.png).

### 07.2bo — проверить fallback BGFX Game View для текстурных материалов (implemented prototype)

Добавлен отдельный Editor Play smoke на существующей тестовой сцене с зарегистрированным текстурным material. Он требует ноль adapter submissions и минимум один textured mesh на прежнем Editor renderer. Отдельный 07.2bn smoke продолжает проверять успешный opaque BGFX путь. Windows x64 Release: focused регрессия **5/5**, полный CTest **102 passed, 1 skipped из 103**; mixed-DPI пропущен из-за отсутствия подходящей пары мониторов. Runtime material API не менялся. [Отчёт](reports/07.2bo-runtime-bgfx-game-view-fallback.md).

### 07.2bn — подключить Runtime BGFX adapter к Editor Play Game View (implemented prototype)

При opt-in `ASTRO_BUILD_RUNTIME_BGFX_3D` Game View передаёт активную runtime Scene в существующий BGFX adapter и использует Editor framebuffer/context. Чистые пакеты рендерятся через adapter; если packet содержит неподдерживаемую текстуру, alpha/material конфигурацию или диагностику, Editor сохраняет прежний полный renderer fallback. Новый Editor smoke запускает Play-копию «камера + куб» и проверяет один adapter submission, GPU cache и Game View screenshot. Вне Windows x64/D3D11 не проверялось; texture/PBR расширение не входит в этот шаг. [Отчёт](reports/07.2bn-editor-game-view-runtime-bgfx.md).

### 07.2bm — Play-session handoff в optional BGFX sample (implemented prototype)

Оконный BGFX smoke запускает Scene через `RuntimeSession`, каждый кадр строит packet из disposable runtime-копии, выполняет fixed-step обновление и проверяет, что authoring Scene не меняется. Подтверждены 2 draw items/submissions, cache из 2 mesh/material вариантов и screenshot. Это первый Play→Scene packet→BGFX sample путь, не подключение Editor/Game View. [Отчёт](reports/07.2bm-runtime-play-bgfx-smoke.md).

### 07.2bl — Runtime BGFX consumer для Scene render packet (implemented prototype)

Создан необязательный `astro_runtime_bgfx_scene3d` adapter: он принимает packet, применяет Camera3D/world transforms, материал baseColorFactor, кэширует geometry buffers по content hash и material и отправляет один BGFX draw на item. Windows x64 Release smoke собрал packet сцены с двумя дочерними mesh объектами и проверил screenshot, 2 items/2 submissions/2 cache entries/0 diagnostics. Full CTest: **100 passed, 1 skipped из 101 за 126,36 с**; пропущен mixed-DPI test без пары подходящих дисплеев. Ограничение: opt-in adapter prototype не подключён к общему Play host или Editor; текстуры, alpha/PBR и другие backends не поддержаны. [Отчёт](reports/07.2bl-runtime-bgfx-packet.md).

### 07.2bk — backend-neutral render packet для 3D Scene (implemented prototype)

Добавлен `astro_scene_render3d`: общий Scene walker формирует активную Camera3D, иерархические world transforms и видимые render items для MeshRenderer/PrimitiveRenderer. Встроенная геометрия создаётся через MeshData и разделяется по виду primitive внутри одного packet. Mesh импорт подключается callback-resolver без зависимости от glTF или backend; зарегистрированный project material override приоритетнее source material. Некорректные объекты пропускаются с entity/asset diagnostics, без камеры пакет не строится. Editor переиспользует общий camera/transform helper. Unit и SceneFile+AssetRegistry integration тесты добавлены. Полный Release CTest: **99 passed, 1 skipped из 100 за 130,54 с**. Это подготовка данных, не GPU draw loop или Play renderer. См. [отчёт](reports/07.2bk-runtime-scene-render-packet.md).

### 07.2bj — архитектурная оценка 3D PrimitiveRenderer для Runtime/Play (assessment complete)

Проверено, что раньше материализация примитивов и BGFX submission жили в Editor, а `engine/runtime` предоставлял только SDL 2D renderer. 07.2bk подготовил backend-neutral packet; 07.2bl добавил отдельный opt-in BGFX consumer и smoke. Эти результаты ещё не подключают renderer к общему Play host. [Отчёт](reports/07.2bj-runtime-3d-assessment.md), [D196](DECISIONS.md#d196--не-считать-bgfx-preview-готовым-runtime-3d-renderer-072bj).

### 07.2bi — проверка project material встроенного примитива в Scene и Game View (implemented prototype)

Расширен Editor smoke: проверяется material draw submission именно для `PrimitiveRenderer` отдельно в Scene View и Game View, причём Game View должен использовать активную Camera3D. Цветовой pixel check теперь использует жёлтый материал, отличимый от красного/зелёного/синего 2D маркеров тестовой сцены. Windows x64 Release/D3D11: **402 жёлтых пикселя**, по 1 submission примитива с материалом в каждом viewport; focused smoke прошёл. Полный CTest: **97 passed, 1 skipped из 98 за 122,36 с**; mixed-DPI smoke штатно пропущен из-за конфигурации дисплеев. Скриншот и ограничения: [отчёт](reports/07.2bi-primitive-material-game-view.md).

### 07.2bh — визуальная проверка материала на примитиве (implemented prototype)

Editor smoke вывел Cube с назначенным проектным материалом в Scene View, проверил draw submissions и сохранил screenshot. Изначальный красный pixel threshold мог совпасть с красным 2D маркером; в 07.2bi он заменён уникальным жёлтым и дополнен Game View submission check. Исторические результаты и уточнение: [отчёт 07.2bh](reports/07.2bh-visible-primitive-material.md), [TESTING](TESTING.md#072bh-материал-примитива-в-scene-view).

### 07.2bg — материалы встроенных примитивов (implemented prototype)

Примитивы принимают проектный Material через Inspector; typed-команда транзакционная. PrimitiveRenderer v2 и Scene v11 сохраняют ссылку, Scene v10 мигрирует с пустой ссылкой. Полный Windows x64 Release CTest: **96 passed, 1 skipped из 97 за 119,62 с**. Расширенное PBR shading, pixel golden и экспортный runtime не входят в этот подэтап. [TESTING](TESTING.md#072bg-материалы-встроенных-примитивов), [отчёт](reports/07.2bg-primitive-material-inspector.md).

### 07.2bf — параметры PrimitiveRenderer в Inspector (implemented prototype)

Inspector редактирует форму и видимость встроенного примитива через транзакционную команду. Unit-тест локализации, command integration Undo/Redo и Editor smoke прошли; полный Windows x64 Release CTest — **96 passed, 1 skipped из 97 за 132,46 с**. Smoke подтверждает отрисовку контролов, но не автоматизирует физическое нажатие Combo/Checkbox. Материалы/цвет и независимая primitive pixel golden остаются будущей работой. [TESTING](TESTING.md#072bf-параметры-primitive-renderer-в-inspector), [отчёт](reports/07.2bf-primitive-inspector-authoring.md).

### 07.2be — транзакционные примитивы и Scene View smoke (implemented prototype)

Создание встроенного примитива и удаление поддерева выполняются typed Scene-командами с dry-run и Undo/Redo; тесты подтверждают устойчивость EntityId/ComponentId и восстановление parent-связей. Editor smoke отдельно передаёт Cube, Plane, Sphere и Cylinder через BGFX Scene View и проверяет 4 mesh submissions без диагностики. Windows x64 Release: targeted build успешен, целевые CTest **4/4 passed**, полный CTest **96 passed, 1 skipped из 97** (126,89 с). Snapshot delete может потреблять много памяти на крупных сценах; pixel golden и иные платформы не проверялись. См. [TESTING](TESTING.md#072be-транзакционные-примитивы-и-scene-view-smoke) и [отчёт](reports/07.2be-primitive-transactions-render-smoke.md).

### 07.2bd — примитивы в 3D-сцене (implemented prototype)

Иерархия создаёт Cube, Plane, Sphere и Cylinder. Компонент сохраняется в Scene v10; геометрия генерируется в памяти и отображается общим Scene/Game View путем. Unit-проверка геометрии, Scene round-trip и текущий Editor smoke прошли. 07.2be добавил транзакционные команды и render-submission smoke; отдельный pixel golden для каждой формы остаётся неготовым. [Результаты](TESTING.md#072bd--примитивы-в-3d-сцене), [отчёт](reports/07.2bd-scene-primitives.md), [07.2be](reports/07.2be-primitive-transactions-render-smoke.md).

### 07.2bc — выбор файла/папки проекта (completed)

В диалогах проекта есть кнопка «Обзор…»: открыть `.astro` файл либо выбрать родительский каталог. Для создания редактор добавляет имя проекта к родительскому пути и создаёт новую дочернюю папку, не перезаписывая существующую. Отмена сохраняет введённый путь, длинный путь не копируется за пределы поля. Windows x64 Release сборка и focused CTest прошли; системные диалоги открывались вручную, композиция пути покрыта unit-тестом. Другие ОС и автоматизация системных диалогов остаются вне этой проверки. [Результаты](TESTING.md#072bc--выбор-пути-проекта), [отчёт](reports/07.2bc-project-path-browse.md).

### 07.2bb — захват drag в Scene View (completed)

Scene View сохраняет right orbit/middle pan после выхода за viewport при нажатии внутри; внешние нажатия игнорируются, release снимает capture, wheel остаётся hover-only. `editor.asset_model_preview.smoke` проверяет рабочий SDL→ImGui input путь в Scene View, движение камеры за границей и нажатия снаружи. Windows x64 Release, BGFX D3D11; synthetic events, не физическая мышь. Результаты сборки/регрессии: [TESTING](TESTING.md#072bb--захват-drag-в-scene-view), [отчёт](reports/07.2bb-scene-view-drag-capture.md).

### 07.2ba — drag capture для Project model preview (completed)

Right orbit/middle pan сохраняются при перемещении за image rect, если drag начался внутри; внешнее нажатие игнорируется, release/смена asset сбрасывает capture. SDL smoke проверяет yaw, focus и конечное released состояние. Windows Release focused **5/5 за 9,39 с**, полный **96/96 за 129,06 с**. Scene View пока использует hover-scoped controls; физическая мышь не проверена. [Отчёт](reports/07.2ba-project-preview-drag-capture.md), [TESTING](TESTING.md#072ba--захват-drag-в-project-model-preview).

### 07.2az — SDL event path для orbit/pan Project preview (completed)

`editor.asset_model_preview.smoke` посылает изолированные SDL motion/button events в окно и прогоняет их через ImGui backend и текущие viewport handlers. Smoke требует изменения camera yaw после правого drag и focus после среднего drag; события выпускают кнопки и остаются внутри окна теста. Windows Release focused CTest **5/5 за 9,22 с**, полный **96/96 за 128,57 с**. Автоматизация проверяет SDL integration path, но не физическое движение мыши. [Отчёт](reports/07.2az-project-preview-mouse-input.md), [TESTING](TESTING.md#072az--sdl-события-мыши-project-preview).

### 07.2ay — Project model preview при resize (completed)

После изменения размера Project dock viewport повторно прокручивается в видимую область. Smoke меняет окно и заканчивается в компактном размере 864×608; полный прямоугольник ImGui image обязан попадать в clip rect. Windows Release focused CTest **5/5**, полный **96/96 за 129,11 с**; viewport видим, screenshot crop содержит 2 202 пикселя модели. Логи и пределы проверки: [отчёт](reports/07.2ay-project-preview-resize.md) и [TESTING](TESTING.md#072ay--project-model-preview-после-resize).

### 07.2ax — ручной review Project model preview (partial validation)

Открыта копия smoke-проекта из `build/` с зарегистрированной glTF из двух разнесённых primitives и source material. Project preview показал обе фигуры; реальное колесо мыши меняло zoom. При узкой панели подтверждён 4:3 аспект (160×120 logical minimum); в restored окне 864×608 нижняя область preview клипуется, в maximized portrait viewport виден полностью. Цвет был сине-голубым, без golden точного совпадения не утверждается. Не проверены реальные right-button orbit и middle-button pan: доступный UI automation слой умеет только left drag. Найденные пользовательские OBJ/FBX неподдерживаемы и не изменялись. Full CTest **96/96 за 122,80 с**. [Отчёт](reports/07.2ax-manual-model-preview-review.md); точные baseline/post commands — в [TESTING](TESTING.md#072ax--manual-project-preview-review).

### 07.2aw — автоматическая проверка Project model viewport (completed)

Smoke в изолированном проекте расширяет тестовую glTF до двух primitives, проверяет два target submissions, Frame All и изменения yaw/focus/distance после orbit/pan/zoom через те же методы, что вызывают GUI input handlers. Screenshot crop: 2 202 видимых пикселя. Windows Release focused CTest **5/5 за 16,76 с**, полный **96/96 за 123,28 с**; baseline до изменений — сборка no-op и **5/5 за 16,85 с**. Подробности и границы утверждений — в [STATUS](STATUS.md#072aw--автоматическая-проверка-project-model-viewport), [TESTING](TESTING.md#072aw--orbitpanzoom-и-модель-с-несколькими-primitives), [D183](DECISIONS.md#d183--автоматическая-проверка-project-model-viewport-072aw) и [отчёте](reports/07.2aw-model-preview-controls.md).

### 07.2av — изолированный 3D viewport для выбранной модели (completed)

Project отображает выбранную зарегистрированную glTF-модель в самостоятельном BGFX viewport с автоматическим Frame All и orbit/pan/zoom камерой. Временная preview Scene и target GPU cache отделены от редактируемой сцены и Scene/Game views. Лимиты: 128 primitives, 64 MiB геометрии; smoke проверяет draw submissions, camera framing, отсутствие mesh diagnostics и скриншотную pixel coverage (2 178 пикселей на smoke fixture). Windows Release: focused **5/5**, полный CTest **96/96 за 123,80 с**. Команды и ограничения — в [отчёте](reports/07.2av-project-model-viewport.md); см. [STATUS](STATUS.md#072av--изолированный-3d-preview-модели), [TESTING](TESTING.md#072av-3d-preview-модели-в-project) и [D182](DECISIONS.md#d182--изолированный-3d-preview-модели-072av).

### 07.2au — previews выбранных зарегистрированных image/model (completed)

Project показывает изображение выбранного AssetId через BGFX texture cache и WIC decoder только по registry cache path. Для model выполняется импорт из зарегистрированного Assets-relative пути и показывается геометрическая сводка: mesh/primitive/vertex/index counts и ошибка импорта. Проверены image и model smoke. Это ещё не отдельный визуальный 3D viewport; preview не трогает Scene и исходные ресурсы, но импорт может заполнить производный Library cache. Windows Release focused CTest **5/5**, полный **96/96 за 131,11 с**. Ограничения и детали: [STATUS](STATUS.md#072au--предпросмотр-зарегистрированных-ресурсов), [TESTING](TESTING.md#072au-предпросмотр-зарегистрированных-ресурсов), [D181](DECISIONS.md#d181--предпросмотр-изображений-и-геометрии-моделей-072au) и [отчёт](reports/07.2au-asset-previews.md).

### 07.2at — read-only Asset Browser из AssetRegistry (completed)

Окно Project показывает зарегистрированные images, models, materials и scripts. Поиск — UTF-8 substring по source-relative path или decimal AssetId; фильтр ограничивает список одним типом. Источник каталога — только in-memory registry maps; filesystem traversal, открытие файлов и изменение ресурсов отсутствуют. Порядок стабилен по AssetId, видимая область использует ImGui list clipper. Windows Release focused CTest **4/4 за 6,32 с**, полный CTest **95/95 за 122,06 с**; baseline до изменений — **93/93 за 128,05 с**. Ограничения: поиск чувствителен к регистру, каталог не показывает незарегистрированные файлы и внешние изменения `.astro/assets.index`, пока проект не открыт заново. См. [TESTING](TESTING.md#072at-read-only-asset-browser), [D180](DECISIONS.md#d180--read-only-asset-browser-на-основе-assetregistry-072at) и [отчёт](reports/07.2at-read-only-asset-browser.md).

### 07.2as — безопасный переход от Game View diagnostics к зарегистрированному ресурсу (completed)


### 07.2ar — переход из Game View diagnostics к проблемному MeshRenderer (completed)

Диагностики текущего кадра становятся отдельными записями с необязательным related EntityId. При ошибках, возникших на конкретном объекте (неразрешённый model/material, подготовка геометрии, bounds/Transform), общий Diagnostics позволяет выбрать объект и перейти к нему в Hierarchy; активная Camera3D использует тот же путь. Глобальные ошибки без надёжной сущности остаются текстом, неизвестные/удалённые EntityId не создают действие. Smoke проверяет вложенную проблемную сущность, раскрытие родителя, выбор и продолжение camera-driven отрисовки корректных мешей. Windows Release focused **5/5 за 13,00 с**, полный CTest **92/92 за 115,27 с**. Проверено только на Windows x64 BGFX D3D11; см. [TESTING](TESTING.md#072ar-переход-от-game-view-diagnostics-к-meshrenderer), [D178](DECISIONS.md#d178--entity-links-for-game-view-renderer-diagnostics-072ar) и [отчёт](reports/07.2ar-game-view-mesh-diagnostic-navigation.md).

### 07.2aq — Game View diagnostics в Console и переход к Camera3D (completed)

Текущая 3D-диагностика Game View отображается в общем Diagnostics окне без копирования одного и того же предупреждения в Recent diagnostics каждый кадр. При невалидной активной камере действие выбирает её EntityId, автоматически раскрывает предков Hierarchy и прокручивает к строке. При отсутствии active camera окно показывает Camera3D-кандидатов и их выбор не активирует камеру. Smoke проверяет оба перехода на временной сцене плюс успешный предыдущий путь. Windows Release focused **4/4 за 9,31 с**; полный CTest **91/91 за 119,77 с**. Первый полный прогон имел единственный нестабильный mixed-DPI smoke; изолированный повтор прошёл 1/1, затем полный повтор — 91/91. Точные команды и логи указаны в [TESTING](TESTING.md#072aq-game-view-diagnostics-в-console-и-переход-к-camera3d) и [отчёте](reports/07.2aq-game-view-diagnostics-console-navigation.md). Схема сцены/API не менялись.

### 07.2ap — диагностики активной камеры в Game View (completed)

3D camera/render diagnostics отображаются в компактном overlay поверх изображения Game View и локализуют отсутствие активной камеры и известные причины ошибки Transform. Ошибка камеры отключает только 3D pass: 2D путь остаётся действующим. Добавлены Localization unit assertions и два временных Editor smoke: no-active camera и active camera with zero-scale pose; обычный `editor.smoke` и camera-driven draw также проверены. Windows Release focused CTest прошёл **4/4**, полный — **91/91 за 115,74 с**. Точные команды и детали указаны в [TESTING](TESTING.md#072ap-диагностики-активной-camera3d-в-game-view) и [отчёте](reports/07.2ap-game-view-diagnostics.md). Формат сцены, command API и пользовательские проекты не менялись.

### 07.2ao — активная Camera3D в Game View (completed)

Подключён Windows BGFX Game View mesh pass: иерархическая Transform камеры задаёт её world position/orientation, lens schema задаёт перспективу, aspect берётся из render target; отдельная Scene View orbit camera не меняется. Game View получает depth target и сохраняет существующие 2D спрайты как последующий overlay. Нет active Camera3D или pose невалидна — 3D пропускается с внутренней диагностикой target, 2D продолжает отображаться. Unit и Editor smoke проверены; полный Release CTest прошёл **89/89 за 116,02 с**. Полноценный Play/runtime camera, UI-вывод Game View diagnostic, материалы/освещение/тени и корректная глубинная композиция 2D с 3D остаются вне этого шага. См. [TESTING](TESTING.md#072ao-активная-camera3d-в-game-view), [D175](DECISIONS.md#d175--активная-camera3d-в-game-view-072ao) и [отчёт](reports/07.2ao-active-camera-game-view.md).

### 07.2an — активная Camera3D и редактирование Inspector (completed)

Camera3D schema v2 хранит active-флаг; Scene проверяет уникальность активной камеры. Typed transactional команды `camera3d.add/remove/set_lens/set_active` обеспечивают dry-run и Undo/Redo, сохраняя ComponentId и предыдущий выбор. Inspector редактирует FOV/near/far с явным Apply и переключает активную камеру; Scene v9 читает v8 Camera3D как неактивную и повышает формат только при явном save. Scene/file/command integration и Editor smoke подтверждают поведение. Camera view-matrix и Game View integration остаются вне этого подэтапа. Подробнее: [TESTING](TESTING.md#072an-активная-camera3d-и-inspector), [D174](DECISIONS.md#d174--активная-camera3d-и-scene-v9-072an), [отчёт](reports/07.2an-active-camera-inspector.md).

### 07.2am — Scene Camera3D: типизированные lens-поля и формат v8 (completed)

Добавлен необязательный компонент перспективной камеры к Entity без изменений семантики существующих сцен: поле зрения в градусах и near/far planes валидируются конечностью и диапазонами; pose остаётся в существующем Transform. Стабильный ComponentId сохраняется в Scene v8; v1–v7 читаются без выдумывания Camera3D, апгрейд происходит только при явном сохранении. Round-trip, отказ на неверном FOV и миграция v7→v8 покрыты integration-тестом. Отдельно исправлена локализованная MSVC/Ninja dependency scan настройка. Clean Release build и полный CTest прошли; детали: [TESTING](TESTING.md#072am-scene-camera3d-и-scene-v8), [D172](DECISIONS.md#d172--scene-camera3d-как-опциональные-данные-авторинга-072am), [D173](DECISIONS.md#d173--локализованный-msvc-showincludes-при-ninja) и [отчёт](reports/07.2am-scene-camera-component.md). Выбор активной камеры и Inspector добавлены в 07.2an; Transform-to-view conversion и Game View draw path остаются следующими отдельными работами.

### 07.2al — автоматический переход окна между дисплеями с разным DPI (completed; системный scale 100% не восстановлен)

Добавлен SDL smoke реального перемещения Editor окна между дисплеями с различным content scale. Проверяются целевой `SDL_GetWindowDisplayScale()`, применённый ImGui scale и восстановление исходного дисплея; отсутствие SDL display/scale событий не мешает проверять обновление через per-frame polling. Тест корректно пропускается, если нет доступной пары с разным scale. На текущей Windows машине пара `1@1.25` и `2@1.0` доступна, smoke прошёл 3/3 повторов, а полный Release CTest повторно прошёл **88/88 за 113,63 с**. Settings показывает у активного дисплея пустые/отключённые Scale, resolution и orientation; другой выбранный дисплей предлагает только включить его обратно. Поэтому безопасного штатного пути к 100% в текущем интерфейсе не найдено, Custom scaling не меняли, тест при 100% остаётся непроверенным. Не применять системные обходы без отдельного явного разрешения.

### 07.2ak — compact visual smoke и SDL геометрия окна (completed; display restore limitation)

Уточнён критерий compact MASK-ramp screenshot: 3D-меши законно закрывают часть 2D-маркеров, поэтому compact 3D alpha smoke проверяет Scene View геометрию/текстуры/alpha, а отдельный общий Editor smoke продолжает проверять все цветовые маркеры. Геометрическая политика окна покрыта unit-тестом; новый SDL integration smoke посылает display-change event после вывода тестового окна за usable area и проверяет конечные координаты и размер. Windows Release CTest: 87/87 на текущем `displayScale=1.25`. Возврат системного масштаба 100% через стандартный UI не завершён: доступный путь открывал Custom scaling с пустым полем, глобальное значение не применялось. Нужен отдельный полный прогон при подтверждённых 100%; переход на дисплей с отличающимся DPI пока не автоматизирован. См. [TESTING](TESTING.md#072ak-compact-visual-smoke-и-sdl-геометрия-окна), [D170](DECISIONS.md#d170--проверяемая-геометрия-editor-окна-и-compact-smoke-072ak) и [отчёт](reports/07.2ak-window-geometry-and-compact-smokes.md). Следующая рекомендация: после возврата 100% проверить полный CTest, затем отдельно автоматизировать перенос между дисплеями с разным DPI.

### 07.2aj — адаптивная начальная раскладка и границы окна (completed; compact checks clarified in 07.2ak)

Начальный размер Editor ограничен `SDL_GetDisplayUsableBounds`, окно центрируется на основном дисплее; смена дисплея, scale или логического размера перепроверяет границы. На ширине окна ниже 1000 логических пикселей Reset layout выбирает компактные пропорции, а Game View ограничивается доступным размером. Unit покрывает широкую, компактную и предельно малую рабочие области. Первый полный CTest при 125% завершился 84/86; compact screenshot false failures были разобраны в follow-up 07.2ak. Settings не позволил подтвердить возврат масштаба 125%→100%; global custom scale не применялся. Геометрия SDL теперь проверяется отдельным smoke, но автоматического переноса на монитор с отличающимся DPI пока нет. См. [07.2ak](reports/07.2ak-window-geometry-and-compact-smokes.md), [TESTING](TESTING.md#072aj-адаптивная-раскладка-editor), [D169](DECISIONS.md#d169--адаптивная-layout-политика-editor-072aj) и [отчёт](reports/07.2aj-responsive-editor-layout.md).

### 07.2ai — DPI-масштабирование Editor UI (completed prototype)

Editor каждый кадр проверяет SDL window display scale. При изменении он восстанавливает Dear ImGui style из базовой копии, масштабирует размеры `ScaleAllSizes()` и `FontScaleMain`; invalid/ниже 1 scale безопасно fallback/clamp в диапазон 1–4. Unit-тест покрывает 100%, 125%, повторное применение, смену коэффициента, возврат и границы. Реальный Windows переход 100→125→100 проверен на работающем окне; HWND DPI изменился 96→120→96. Smoke при 125% вывел `displayScale=1.25`, `uiScale=1.25`, `fontScale=1.25`, padding 10; на 100% — 1/1/8. Финальный Windows Release CTest: 85/85 за 109,00 с. Проверка показала, что узкая рабочая область может обрезать фиксированные панели; этот layout не перерабатывался. См. [TESTING](TESTING.md#072ai-dpi-масштабирование-editor-ui), [D168](DECISIONS.md#d168--масштабировать-editor-ui-от-sdl-display-scale-072ai) и [отчёт](reports/07.2ai-editor-dpi-scaling.md).

### 07.2ah — оценка DPI-масштабирования Editor UI (completed assessment)

Проверить связь подтверждённого SDL display scale с Dear ImGui в Editor. SDL3 backend перечисляет мониторы и их content scale, но Editor задаёт шрифт 16 px и не включает `io.ConfigDpiScaleFonts`; style spacing/padding также явно не масштабируются. Настройка font scaling сама по себе не решает масштабирование всего layout, а `DisplayFramebufferScale` относится к координатам framebuffer и не должна подменять DPI scale. UI и системные настройки не менялись. Полный Release CTest: 84/84 за 90,72 с. Следующий рекомендуемый подэтап 07.2ai — реализовать и проверить единый DPI policy для шрифтов и style, включая мониторные переходы, без изменения пользовательского scale.

### 07.2ag — диагностика SDL Windows DPI metrics (completed)

Сопоставить Windows HWND DPI-awareness/DPI с SDL `SDL_GetWindowPixelDensity` и `SDL_GetWindowDisplayScale` при фактических 125%. Живое окно было PerMonitorV2 с DPI 120; SDL вернул `pixelDensity=1` и `displayScale=1.25`. Эти метрики различны: первая описывает отношение pixel-size к window-size, вторая отражает display content scale. Значение density 1 не доказывает, что Windows scale не распознан. Добавлен только `displayScale` в smoke лог. Profile smoke прошли 6/6; полный CTest при восстановленных 100% — 84/84. См. [TESTING](TESTING.md#072ag-диагностика-windows-dpi-metrics-sdl), [D166](DECISIONS.md#d166--разделять-sdl-display-scale-и-pixel-density-072ag) и [отчёт](reports/07.2ag-sdl-windows-dpi-metrics.md).

### 07.2af — проверка на реальном Windows DPI >100% (environment-limited assessment)

При явном разрешении пользователя временно выставить 125% на основном и втором дисплеях, повторить ramp smoke и восстановить 100%. Windows изменил logical desktop bounds; смоуки прошли 6/6. Первичный лог содержал только `pixelDensity=1.0`, поэтому последующая диагностика 07.2ag проверила второй SDL metric и подтвердила `displayScale=1.25` при HWND DPI120/PerMonitorV2. Системные настройки восстановлены. См. [TESTING](TESTING.md#072af-проверка-на-реальном-windows-dpi), [D165](DECISIONS.md#d165--временное-изменение-масштаба-только-для-проверки-072af) и [отчёт](reports/07.2af-active-windows-dpi-assessment.md).

### 07.2ae — пространственные MASK smoke при разных размерах окна (completed prototype)

Проверить near/far alpha-рампу на логических размерах 900×650, 1100×720 и 1440×900; последний запускается с SDL high-pixel-density request. Классификация использует долю пикселей внутри каждого из 64 вертикальных bins, чтобы не привязывать критерий к разрешению, и допускает начало последовательности как с MASK, так и с blend-through. Windows x64 Release: 6/6 профильных и 84/84 полных CTest прошли. Фактическая pixel density всех окон на этом дисплее была 1.0, поэтому физическое DPI >1 не проверено. См. [TESTING](TESTING.md#072ae-пространственные-mask-smoke-при-разных-размерах-окна), [D163](DECISIONS.md#d163--пространственные-mask-smoke-при-разных-размерах-окна-072ae) и [отчёт](reports/07.2ae-window-density-spatial-smoke.md).

### 07.2ad — пространственная проверка MASK alpha-рампы (completed prototype)

Разбить crop Scene View на 64 вертикальные колонки. В двух mip smoke проверять, что на стороне низкого U первым обнаруживается blend-rich участок, далее чередуются несколько mask-rich и blend-rich областей. Классификация допускает широкие различия числа пикселей, не является golden-image сравнением. Результаты: [TESTING](TESTING.md#072ad-пространственная-проверка-mask-alpha-рампы), [D162](DECISIONS.md#d162--пространственные-проверки-mask-alpha-рампы-072ad), [отчёт](reports/07.2ad-mask-spatial-transition.md). Проверена только текущая Windows D3D11 конфигурация.

### 07.2ac — локальная alpha-рампа вокруг MASK cutoff (completed prototype)

Проверить две D3D11 Scene View выборки гладкой UNORM8 alpha-рампы 112–143 через MASK cutoff 0,5 с mip-фильтрацией при UV-scale 2/4. Оба масштаба должны сохранить видимые MASK и blend-through участки. Результаты и ограничения: [TESTING](TESTING.md#072ac-локальная-alpha-рампа-вокруг-mask-cutoff), [D161](DECISIONS.md#d161--локальная-alpha-рампа-вокруг-mask-cutoff-072ac) и [отчёт](reports/07.2ac-mask-cutoff-transition-smoke.md). Это только текущий Windows D3D11 адаптер.

### 07.2ab — смешанные alpha-полосы около MASK cutoff (completed prototype)

Добавить две Windows D3D11 Scene View проверки одной текстуры с вертикальными участками alpha 127/128/129 при cutoff 0,5 и mip-фильтрации. На UV-scale 2 и 4 должны одновременно наблюдаться сохранившиеся MASK-пиксели и отбрасывание ниже-cutoff участка с видимой подложкой. Результаты и ограничения: [TESTING](TESTING.md#072ab-смешанные-alpha-полосы-mask-cutoff), [D160](DECISIONS.md#d160--смешанные-alpha-полосы-masked-mip-072ab), [отчёт](reports/07.2ab-mixed-mask-boundary-smoke.md). Проверка ограничена текущим Windows D3D11 адаптером.

### 07.2aa — MASK cutoff-edge UNORM8 mip smoke (completed prototype)

Add six D3D11 Scene View smoke cases for constant 8-bit alpha bytes 127, 128 and 129 against MASK cutoff 0.5, at UV scales 2 and 4 with mipmapped filtering. Below-cutoff samples must discard; the first representable byte at/above the cutoff and its successor must remain visible. All six pass; see [TESTING](TESTING.md#072aa-mask-cutoff-edge-unorm8-mip-smoke), [D159](DECISIONS.md#d159--mask-cutoff-edge-unorm8-mip-smoke-072aa), and [report](reports/07.2aa-mask-cutoff-edge-smoke.md). Since UNORM8 cannot encode alpha exactly equal to 0.5, value 128/255 is the first representable passing sample, not mathematical equality. Validation is specific to Windows D3D11.

### 07.2z — partial-alpha MASK cutoff minification sweep (completed prototype)

Add D3D11 Scene View smoke cases for the partial-alpha ramp at MASK cutoffs 0.25 and 0.75, both at UV scales 2 and 4; retain the 0.5 cases. All four focused screenshots passed with a submitted MASK mip chain and visible mask/blend regions. Full regression results are in [TESTING](TESTING.md#072z-partial-alpha-mask-cutoff-minification-sweep), [D158](DECISIONS.md#d158--partial-alpha-mask-cutoff-minification-sweep-072z), and the [report](reports/07.2z-partial-alpha-mask-cutoff-smoke.md). Validation is limited to this Windows D3D11 path.

### 07.2y — partial-alpha MASK minification smoke (completed prototype)

Exercise an alpha ramp with partially transparent texels in the Windows D3D11 Editor Scene View at two minification scales and trilinear mip filtering. Both disposable-project smokes passed with one MASK mipmapped submission and visible partial magenta mask plus blend-through pixels; full CTest result is recorded in [TESTING](TESTING.md#072y-partial-alpha-mask-minification-smoke), [D157](DECISIONS.md#d157--partial-alpha-mask-minification-smoke-072y), and the [report](reports/07.2y-partial-alpha-mask-smoke.md). This is a bounded current-driver visual check, not cross-GPU equivalence.

### 07.2x — fractional MASK alpha and cutoff sweep (completed assessment)

Extend nearest-achievable MASK mip coverage checks to stepped and ramped partial-alpha input, with cutoffs 0.25, 0.50 and 0.75, at even 64×64 and odd 31×17 source sizes. No selector, API, schema or dependency change was required: all tested mips already matched the nearest representable coverage. Focused Release unit passed **1/1**; full CTest passed **66/66 in 39.27 s**. See [TESTING](TESTING.md#072x-fractional-mask-alpha-and-cutoffs), decision [D156](DECISIONS.md#d156--fractional-alpha-mask-coverage-sweep-072x) and [report](reports/07.2x-fractional-mask-alpha.md). Tiny mip levels remain quantized.

### 07.2w — MASK mip coverage accuracy assessment (completed)

Verify nearest-achievable cutoff coverage for split, checker and center-hole alpha at even and odd source dimensions, across every generated mip. Replace the search that could select a non-nearest 8-bit result with deterministic threshold-candidate evaluation using the same quantization for scoring and output. Windows x64/MSVC Release unit and full regression passed; full CTest **66/66 in 39.38 s**. One-pixel and some 2×2 levels remain inherently quantized. See [TESTING](TESTING.md#072w-mask-mip-coverage-accuracy), [D155](DECISIONS.md#d155--mask-mip-coverage-candidate-selection-072w), and the [report](reports/07.2w-mask-mip-coverage-precision.md).

### 07.2v — additional MASK alpha patterns (completed prototype)

Extend the temporary mipmapped MASK smoke fixture with a block checker and a centered transparent hole. Keep the split fixture as regression. Checker/hole and both split cases passed; Windows x64 Release full CTest passed **66/66 in 37.95 s**. Scope remains the current D3D11 fixtures; exact LOD and arbitrary-image behavior are not established. See [TESTING](TESTING.md#072v-mask-alpha-pattern-smoke), [D154](DECISIONS.md#d154--additional-mask-alpha-mip-patterns-072v), and the [report](reports/07.2v-mask-mip-pattern-smoke.md).

### 07.2u — nonuniform MASK alpha mip smoke (completed prototype)

Render a disposable 64×64 nonuniform alpha image in Scene View with MASK and mipmapped filtering at two UV minification scales. Both Windows D3D11 smoke cases passed; full Release CTest passed **64/64 in 32.32 s**. Scope is this adapter and fixture; no other renderer or arbitrary image asset is implied. See [TESTING](TESTING.md#072u-mask-alpha-mip-smoke), [D153](DECISIONS.md#d153--nonuniform-mask-alpha-mip-smoke-072u), and the [report](reports/07.2u-mask-mip-coverage-smoke.md).

### 07.2t — rendered MASK cutoff boundary smoke (completed prototype)

The Editor smoke renders three material alpha values around the fixed cutoff and checks cropped Scene View pixels: below must discard; equality and above must retain the mask. Windows x64 Release full CTest passed **62/62 in 27.45 s**. Results are recorded in [TESTING](TESTING.md#072t-mask-cutoff-pixel-smoke), [D152](DECISIONS.md#d152--rendered-mask-cutoff-boundary-validation-072t), and the [report](reports/07.2t-mask-cutoff-pixel-smoke.md). This validates this mask shader/sample path, not all GPU drivers or arbitrary image assets.

### 07.2s — alpha-aware base-color mip chains (completed prototype)

BLEND premultiplied-alpha color filtering and MASK cutoff-coverage compensation are active in the Windows Scene View texture preparation path. Source images/material schemas are preserved; the GPU cache key includes alpha policy/cutoff. Unit coverage checks color fringe, coverage retention and cutoff boundaries. Release full CTest passed 59/59. Exact coverage is quantized and may not be representable at tiny mip dimensions. See [D151](DECISIONS.md#d151--alpha-aware-base-color-mips-072s), [TESTING](TESTING.md#072s-alpha-aware-base-color-mips), and the [report](reports/07.2s-alpha-aware-base-color-mips.md).

## Зафиксированное направление продукта

Цель — универсальный модульный движок 2D/3D для разных жанров с графическим редактором и ИИ как ключевым путём авторинга. AI adapter и сетевой провайдер не требуются для обычного запуска движка или игры. Puzzle остаётся сквозным образцом для проверки основы, а не целевым жанром продукта. AI-ready Command API, схемы, транзакции, разрешения и отчёты учитываются в ранних этапах; этап 09 добавляет интеграцию и AI Game Tester, а не впервые вводит архитектуру для ИИ. Это целевая дорожная карта; фактические возможности не считаются готовыми без тестов и отчёта. См. [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md) и [решение D067](DECISIONS.md#d067-универсальный-ai-first-профиль-продукта).

## 07.1a — 3D camera/mesh contract and procedural bgfx smoke (implemented prototype)

Added optional backend-neutral `astro_runtime_render3d` with validated right-handed camera view/projection matrices and bounded indexed triangle mesh data, plus a colored procedural cube. The separate Windows D3D11/bgfx smoke uses the existing dependency cache, performs thresholded pixel readback, and reports VSync CPU frame-interval p50/p95. Across three isolated-cube runs, median p50 was 16.735 ms and median p95 32.824 ms (ranges 16.701–16.764 and 32.055–34.120 ms); this is not a target budget or complete scene frame time. All three pixel checks found 85,155 foreground pixels, 169 color buckets and bounds 280,135–658,498; it is not a golden-image comparison. Windows Release build completed 161/161 actions and full CTest passed 46/46 in 13.94 s after readback was added. Scene serialization, Editor Scene/Game View, importer/materials/lighting, and 3D physics remain out of scope. See [3D prototype guide](3D_RENDERING.md), [report](reports/07.1a-3d-camera-mesh-prototype.md), [D123](DECISIONS.md#d123--3d-camera-mesh-prototype-boundary-071a).

## 07.1b — bounded glTF/GLB static mesh importer (implemented prototype)

An opt-in `astro_gltf` target now parses `.gltf` and `.glb` geometry through pinned fastgltf v1.0.1 with locally staged, hash-verified simdjson 5.1.0 and simdutf 9.3.3; implicit CMake downloads are disabled. The initial output is backend-neutral static triangle meshes with POSITION and optional indices. Model paths and external buffers are constrained to regular files within project `Assets`; source assets remain untouched and checked geometry caches are generated under `Library/Derived/gltf`. Windows x64 Release build: 95/95 actions; full CTest: 42/42 in 8.92 s. A separate default headless Release build succeeded with the importer disabled and no importer target. This does not yet load meshes into Scene/AssetRegistry/Editor, and it does not import materials, images, UVs, normals, node transforms, animation, skinning, morphs or compression. No 3D physics adapter has been selected. Fuzz/sanitizer, macOS and mobile validation remain pending. See [import guide](3D_RENDERING.md), [asset pipeline](ASSET_PIPELINE.md#optional-gltf-geometry-import-071b), [report](reports/07.1b-bounded-gltf-importer.md), and [D125](DECISIONS.md#d125--optional-bounded-gltf-static-mesh-importer-071b).

## 07.1c — Scene model asset references (implemented prototype)

Scene v6 adds a typed `MeshRenderer` component referencing a stable Model AssetId, with mesh/primitive selection and visibility. Asset index v3 adds bounded `.gltf`/`.glb` records with source size/hash and importer content digest; `GltfImporter::importAndRegister` connects successful import to registry registration. The typed `entity.set_mesh_renderer` command supports validation, execution, audit and undo/redo. Legacy Scene v1–v5 and Asset index v1–v2 remain readable and are written in the current schema only after explicit save/registration. Windows x64 Release clean build and full CTest results are recorded in [testing](TESTING.md#071c-scene-model-asset-references) and the [substage report](reports/07.1c-scene-model-asset-reference.md). This is data/command integration only: Editor/Game View rendering, prefab serialization, per-buffer dependency records, materials, normals, UVs and transform/node import are outside 07.1c.

## 07.1d — Scene MeshRenderer resolution (implemented prototype)

The optional `astro_gltf` module now resolves a Scene `MeshRendererComponent` to its selected backend-neutral primitive. Resolution checks the registered AssetId and source fingerprint, imports/loads current geometry, compares the persisted importer digest (including external buffers), and validates mesh and primitive indices. Stale or invalid references fail with a diagnostic; explicit reimport/register is needed to refresh changed content. The resolver returns geometry but performs no draw call and does not connect Scene/Game View. See [testing](TESTING.md#071d-scene-meshrenderer-resolution), the [3D guide](3D_RENDERING.md#scene-mesh-resolution-071d), and [report](reports/07.1d-scene-mesh-resolution.md).

## 07.1e — Standalone Scene Mesh BGFX preview (implemented prototype)

The optional Windows D3D11 sample now loads a serialized Scene v6 fixture and model, resolves the Scene `MeshRenderer` through AssetRegistry and the bounded importer, uploads the selected primitive, and renders it through BGFX. A CTest wrapper validates the reported selection and framebuffer pixel coverage using a temporary project copy. The existing procedural cube smoke remains intact, and an importer-off build was checked. This is a standalone sample/smoke only: Editor Scene/Game View, gameplay runtime, Scene Transform/hierarchy, multi-object drawing and production shader/material support are not included. See [TESTING.md](TESTING.md#071e-standalone-scene-mesh-bgfx-smoke), [sample instructions](../samples/3d_prototype/README.md), and [report](reports/07.1e-scene-bgfx-preview.md).

## 07.1f — Editor Scene View MeshRenderer preview (implemented prototype)

Optional importer-enabled Editor submits visible Scene `MeshRenderer` components through a separate BGFX 3D pass composed into Scene View. GPU vertex/index buffers are cached per entity/reference, and parent/local transforms are composed before submission. 07.1g adds a backend-neutral orbit camera: right-drag orbits, middle-drag pans, wheel zooms, and Frame All fits transformed bounds of visible mesh geometry. 07.1h expands the isolated smoke to two valid meshes and one missing AssetId: it checks continued rendering, combined center `x=4`, a missing-reference diagnostic, and a coarse white-pixel threshold within the normalized Scene View crop of the swapchain screenshot (**11,900** pixels in the measured run). 07.1i adds `astro_editor.exe --benchmark-3d` with 24 warm-up and 48 measured frames for each of 1/100/500/1000 deterministic mesh scenes. Across three Windows D3D11 runs, the 1000-mesh Scene View CPU pass measured **1.42–1.60 ms p50** (mesh prepare plus submission); complete editor-frame wall time was **16.65–16.68 ms p50** under VSync. Full Release CTest passed **51/51 in 15.60 s**. This establishes a measurement baseline only; it does not set budgets or prove uncapped throughput. BGFX GPU timer includes the complete window frame/VSync, not just the 3D pass. Limitations: static positions/colors, explicit Reload after source/derived-cache changes, no normals/materials/light/picking or Game View/runtime integration; Windows D3D11 only. See [07.1i report](reports/07.1i-editor-3d-frame-profile.md), [TESTING.md](TESTING.md#071i-editor-3d-frame-profile), [3D rendering notes](3D_RENDERING.md#editor-3d-frame-profile-071i), [D132](DECISIONS.md#d132--editor-3d-frame-profile-scope-071i). The material/PBR assessment is complete in 07.2a; implementation is split into future 07.2b+ sub-stages as laid out below.

## 07.2a — Material authoring/PBR contract assessment (completed assessment)

The master plan groups Material Editor, PBR, lighting and shadows into 07.2. This assessment defines the authoring and renderer boundary without picking a `.material` serialization or compiling a new shader. Scene authoring must reference reusable material assets by stable AssetId; source glTF primitive slots and explicit project overrides remain distinguishable; texture references are typed asset references; GUI/visual-logic/AI edits share typed commands and Undo/Redo; defaults, ranges, alpha behavior and missing-reference diagnostics are schema-visible. The PBR core follows a metallic-roughness model with explicit linear/sRGB and texture-channel rules, while lighting/environment and shadow state remain separate Scene/render inputs. The current renderer is only position/vertex-color with prebuilt upstream cube shaders; current importer drops material, normal, UV and texture information. Open items and a proposed vertical-slice order are in [07.2a assessment](reports/07.2a-material-authoring-contract.md), [D133](DECISIONS.md#d133--material-authoring-contract-before-implementation-072a), and [3D_RENDERING.md](3D_RENDERING.md#materialpbr-authoring-assessment-0722a).

## 07.2b — Versioned material asset and AssetRegistry support (completed)

Implemented a bounded `ASTRO_MATERIAL 1` value asset with validated metallic-roughness factors, alpha settings, normal/occlusion controls and typed image AssetId/texCoord roles. AssetRegistry records materials in shared Asset index v4, reads legacy v1–v3, preserves IDs on same-path registration, and detects source hash drift and unresolved image IDs. Focused and full Windows x64 Release verification is recorded in [TESTING.md](TESTING.md#072b-material-asset-format-and-registry); details and exact boundaries are in the [07.2b report](reports/07.2b-material-asset-and-registry.md), [FILE_FORMATS.md](FILE_FORMATS.md#material-asset-v1-072b), and [D134](DECISIONS.md#d134--versioned-material-asset-and-registry-072b). This is not yet user-facing material editing: no Scene slots/overrides, command/Undo operations, glTF material preservation, PBR shader, texture upload, lighting, shadows or Game View rendering.

## 07.2c — Bounded glTF material metadata import (completed)

The optional glTF importer now returns core metallic-roughness material values, five supported texture-role mappings, texture-to-image/sampler indices, image source metadata and per-primitive optional material indices. External local images are confined to `Assets/`, read-only hashed inputs; missing local images remain referenced with diagnostics, remote image URIs are not fetched, and path traversal/reparse paths fail closed. Embedded image bytes are represented by source kind, size and digest; the importer does not decode or register them as project image assets. Unsupported material fields/extensions surface diagnostics. Windows x64 Release focused/full verification and limits are recorded in [TESTING.md](TESTING.md#072c-gltf-material-metadata-import), [3D_RENDERING.md](3D_RENDERING.md#bounded-gltf-material-metadata-072c), [07.2c report](reports/07.2c-gltf-material-metadata.md), and [D135](DECISIONS.md#d135--bounded-gltf-material-metadata-import-072c). Scene material slots, `.astro-material` generation from source materials, normals/UVs, rendering, texture upload, Inspector, lights and shadows remain out of scope.


## 06.28 — Язык интерфейса Editor

Реализован переключаемый English/Russian интерфейс Windows Editor с пользовательским сохранением настройки и тестом покрытия UI-строк. Перевод runtime/SDL demo, низкоуровневых и динамических диагностик не входит в эту ограниченную поставку. Детали и остаточные ограничения: [LOCALIZATION.md](LOCALIZATION.md), [отчёт 06.28](reports/06.28-editor-russian-localization.md).

## 06.29 — Локализация SDL Game UI и меток диагностики

SDL Game UI demo предоставляет выбор English/Russian на экране настроек. Переводятся оболочка меню/HUD, встроенные названия настроек и известные ошибки GameUiModel/адаптера. Неизвестное сообщение сохраняется как техническая деталь с русской меткой; текст SDL/драйвера остаётся источником на языке backend. См. [отчёт 06.29](reports/06.29-runtime-and-diagnostics-localization.md).

## 06.30 — Сохранение языка runtime и диагностики предпочтений

Runtime Game UI сохраняет выбранный `en|ru` в versioned user preference под SDL user-data root; project и game saves остаются независимыми. Чтение, round-trip, malformed preference и реальная настройка через UI проверяются unit/smoke тестами на Unicode-пути и test-owned temporary folder. Динамические ошибки записи получают точный русский префикс с сохранением исходной системной детали. Детали и результаты: [отчёт 06.30](reports/06.30-runtime-language-persistence.md).

## 06.31 — Ручные сохранения кампании в Editor Game View

Editor Game View теперь использует существующий SaveStore/PuzzleCampaignSaveAdapter для слотов 0–9. В профиль явно включены только Puzzle snapshot, ID кампании/уровня и END; user-data путь наследует project settings и добавляет `PuzzleCampaigns/<campaign-id>`. Load сначала валидирует слот и текущие авторские правила, затем заменяет активную campaign session; ошибка не меняет игру. Слоты ручные: autosave, migrations и previews отсутствуют. Smoke также проверяет пустой слот и project/user-data separation. Результаты: [отчёт 06.31](reports/06.31-editor-campaign-progress.md).

## 06.32 — Ручная проверка UI сохранений кампании

Живая проверка выявила, что исходный first-use размер Game View обрезал кнопки Save/Load и не оставлял достаточно места для поля доски. Для обычного интерактивного Editor задан начальный размер 520×560, кнопки Save/Load расположены отдельной строкой; smoke/benchmark сохраняют прежнюю геометрию для стабильности графической регрессии. В настоящем Release Editor на изолированной копии sample-проекта сохранён слот 0, легальный ход изменил счёт 0→180, а Load восстановил `intro`, исходную доску и счёт 0. Файл появился только под SDL user data. Следующий шаг с миграцией ID кампаний не выполнялся. Точные результаты: [отчёт 06.32](reports/06.32-editor-campaign-save-ui.md).

## 06.33 — Identity и границы переноса campaign slots

Каталог сохранений вычисляется централизованно и проверяемо: SDL user-data root + project settings subdirectory + `PuzzleCampaigns/<campaign-id>`. Изменение ID кампании выбирает новый каталог и не перемещает/удаляет старый. Перенос или копия проекта с `.astro/puzzle-save.settings` сохраняет тот же относительный namespace; поэтому такие копии используют общие слоты, пока пользователь не задаст отдельный каталог в настройках. Проект без sidecar использует path-hash default текущего корня. Автоматической миграции нет. Smoke проверяет Unicode, копию sidecar, разделение campaign ID и отказ небезопасного ID, не создавая игровые слоты и не обращаясь к user AppData. [Отчёт 06.33](reports/06.33-campaign-save-identity.md).

Источник: раздел 10 требований пользователя. Подэтапы 00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.10 и 06.1–06.36 реализованы в ограниченном объёме по отчётам. Остальные этапы запланированы; наличие документов само по себе не означает прохождение приёмки.

## 06.34 — Оценка безопасного переноса campaign slots (design only)

Проверены реальные контракты namespace и загрузчика: копия sidecar сохраняет общий каталог, а save payload проверяет точный campaign ID. Поэтому перенос между разными campaign ID не совместим с текущим загрузчиком и не должен выполняться как файловое копирование. Для будущего переноса одинакового ID сначала требуется read-only preview по ограниченным слотам 0–9, включая целостность/backup состояние и конфликты назначения; действующая папка-источник должна оставаться нетронутой. Смена namespace, копирование файлов и преобразование payload не реализованы. Для любых записей потребуется отдельное подтверждение непосредственно перед операцией. [Отчёт 06.34](reports/06.34-campaign-save-transfer-assessment.md).

Источник: решение D096 и отчёт 06.34. Подэтап выполнен как анализ без кода и без доступа к сохранениям пользователя.

## 06.35 — Read-only preview сохранений кампании (implemented, API only)

Добавлены read-only открытие SaveStore и `previewPuzzleCampaignSaveTransfer`: без создания каталогов preview проверяет ровно слоты 0–9, целостность и fallback на `.bak`, ID кампании, обязательные поля, размеры ожидаемых файлов, конфликты primary/backup на цели и неожиданные исходные артефакты. Проверки пути отклоняют `..`, symlink и Windows reparse points. Integration fixture покрывает Unicode path, пустые/валидные/повреждённые/восстановленные слоты, чужой ID, конфликты, отсутствие записи и неизменность source bytes. Перенос файлов, согласие на запись и валидация сохранённого snapshot против текущих правил кампании не входят. [Отчёт 06.35](reports/06.35-campaign-save-transfer-preview.md).

## 06.36 — Editor UI для read-only preview переноса сохранений (implemented)

В `Puzzle Save Settings` добавлено **Preview save transfer**. Источник берётся из текущей настройки проекта, цель — из редактируемого поля каталога. UI показывает результаты 10 слотов, размер, совпадение ID, резервные копии, конфликты и неожиданные артефакты, используя только API 06.35. Preview очищается при правке целевого пути. Получение Windows data root читает `%APPDATA%` и не вызывает `SDL_GetPrefPath`, чтобы интерфейс не создавал каталог. Smoke использует временный каталог, проверяет отображение preview и неизменность отсутствующей цели и исходного файла. Копирования, сохранения настройки, миграции и разрешения конфликтов нет. Preview не гарантирует совместимость snapshot с текущими правилами уровней. Полный Release CTest — **52/52**, 78,53 с; см. [06.36 report](reports/06.36-campaign-save-transfer-editor-preview.md) и [D098](DECISIONS.md#d098--show-campaign-save-transfer-preview-in-editor-read-only-0636).

## 06.37 — Revalidation contract для переноса campaign saves (implemented, no-write)

Добавлен `revalidatePuzzleCampaignSaveTransferPreview`: непосредственно перед будущим переносом он повторно получает preview и возвращает `Ready`, `StaleSource`, `StaleDestination`, `DestinationConflict` или `SourceNotTransferable`. Для обнаружения изменений содержимого первичных и `.bak` файлов preview хранит ограниченный FNV-1a отпечаток (файлы больше лимита формата не допускаются к preflight). Это только обнаружение случайного устаревания, не криптографическая защита. Любая существующая запись по ожидаемому target slot path считается конфликтом, включая пустой файл или каталог. Невалидные/чужие/слишком большие слоты и неожиданные source artifacts блокируют `Ready`; пустые слоты разрешены, если есть хотя бы одно допустимое сохранение. Никакой файл не копируется/заменяется, не создаются каталоги; каждый будущий перенос по-прежнему требует нового просмотра и явного подтверждения. Release full CTest **52/52**, 54,46 с; см. [06.37 report](reports/06.37-campaign-save-transfer-preflight.md) и [D099](DECISIONS.md#d099--revalidate-campaign-save-transfer-preview-before-write-0637).

## 06.38 — Copy-only transaction для campaign saves (runtime API, без UI)

`copyPuzzleCampaignSaveTransfer` принимает только подтверждённый вызывающей стороной запрос и повторно выполняет 06.37 preflight до создания target-папки. Копируются только primary/`.bak` артефакты распознанных same-ID слотов, побайтно. Создание цели эксклюзивное (`CREATE_NEW` на Windows, `O_EXCL` на POSIX); существующие файлы не перезаписываются. При отмене/ошибке удаляются лишь файлы, созданные этой попыткой и всё ещё совпадающие с её выходом, а пустые созданные папки удаляются в обратном порядке. Конкурирующий файл сохраняется; невыполненный rollback возвращает список артефактов. Это rollback при обнаруженной ошибке, не crash-atomic транзакция: аварийное завершение может оставить частичный набор. Прогресс callback может отменить операцию. UI-кнопки и реальные каталоги пользователя не подключены. Тесты копируют только изолированные temp fixtures. Release CTest **52/52**, 59,88 с. См. [06.38 report](reports/06.38-campaign-save-copy-transaction.md) и [D100](DECISIONS.md#d100--same-id-copy-only-campaign-save-transaction-0638).

## 06.39 — Подтверждённое копирование сохранений кампании в Editor

`Puzzle Save Settings` связывает UI с copy API 06.38. После read-only preview Editor предлагает отдельный диалог подтверждения с точными source/target путями, числом валидных слотов, объёмом данных и предупреждением о создании файлов. Кнопка подтверждения вызывает повторную проверку и копирование; существующие файлы не заменяются. Результат показывает число файлов/байт, диагностику и оставшиеся пути при неполном rollback. Smoke проводит тот же путь подтверждения на собственной временной Unicode-фикстуре и сверяет байты; реальные пользовательские сохранения не затрагиваются проверкой. Миграции нет, аварийное завершение всё ещё может оставить частичную цель. См. [06.39 report](reports/06.39-campaign-save-transfer-editor-confirmation.md) и [D101](DECISIONS.md#d101--editor-confirmation-before-campaign-save-copy-0639).

## 06.40 — Журнал и диагностика прерванного переноса

Перед записью слотов runtime эксклюзивно создаёт ограниченный `ASTRO_CAMPAIGN_TRANSFER 1` journal с ID кампании и ожидаемыми именами/размерами/отпечатками файлов; запись сбрасывается на диск до копирования. Успешная операция и штатный rollback удаляют только созданный этой операцией неизменённый journal. Если процесс прекращён между файлами, следующий preview обнаруживает journal, классифицирует каждый target artifact как полный, частичный, отсутствующий, изменённый или небезопасный; новая копия блокируется. Editor показывает journal/artifact пути и требует отдельного подтверждения, чтобы удалить только запись журнала. Save-файлы этим действием не меняются; частичные/изменённые файлы пользователь проверяет вручную. Integration test действительно завершает дочерний процесс после первого файла на Unicode temp fixture и проверяет состояние после повторного запуска тестового процесса. Это обнаружение process interruption, не атомарный commit и не гарантия устойчивости при отключении питания. См. [06.40 report](reports/06.40-campaign-save-transfer-recovery-journal.md) и [D102](DECISIONS.md#d102--journal-and-report-campaign-save-transfer-interruption-0640).

## 06.41 — Явное продолжение прерванного переноса

Добавлен отдельный `resumePuzzleCampaignSaveTransfer` и подтверждающий диалог Editor. Runtime сверяет неизменность журнала, кампанию, состав/содержимое source primary и `.bak` файлов с записью журнала и повторно классифицирует цель непосредственно перед записью. Полные target-файлы пропускаются; отсутствующие создаются только эксклюзивно. Изменённый источник возвращает `StaleSource`; изменившаяся после просмотра цель требует повторной диагностики; Partial/Changed/Unsafe блокируются без записи. При сбое/отмене откатываются только файлы, созданные текущим продолжением, а журнал остаётся для следующего просмотра. Integration test прерывает дочерний процесс, проверяет отказ без подтверждения, успешное продолжение, изменённую цель и устаревший источник. Editor smoke подтверждает отображение действия продолжения. Это не решает ручную очистку частичных файлов, повреждённый журнал, конфликт двух одновременных процессов или устойчивость к отключению питания. См. [06.41 report](reports/06.41-campaign-save-transfer-resume.md) и [D103](DECISIONS.md#d103--explicit-resume-for-campaign-save-transfers-0641).

## 06.42 — Политика частичных файлов переноса

Частичная запись target остаётся конфликтом: runtime resume отказывает с `DestinationConflict`, не удаляет её, не меняет содержимое и оставляет журнал. Editor показывает отдельное предупреждение и отключает resume, пока среди артефактов есть Partial. Это намеренно: журнал сообщает ожидаемый полный fingerprint, но не хранит fingerprint частичного префикса и не аутентифицирует происхождение target-файла. Совпадение байтов с началом source не доказывает, что файл создала именно эта операция; поэтому append/delete/replace-авторизация не добавляется. Integration test проверяет префиксный partial target после имитации process interruption и подтверждает неизменность его байтов, отсутствующей пары и журнала; Editor smoke проверяет предупреждение и заблокированное состояние. Дальнейшая ручная очистка — только после пользовательского просмотра вне автоматического resume. См. [06.42 report](reports/06.42-partial-campaign-save-transfer-policy.md) и [D104](DECISIONS.md#d104--preserve-partial-campaign-save-targets-0642).

## 06.43 — Диагностика повреждённого журнала переноса

`PuzzleCampaignSaveTransferRecovery` сообщает отдельную причину непригодности журнала: `Unreadable`, `Unsafe`, `Oversized` или `Malformed`. Неизвестная/усечённая структура, будущая версия, небезопасный тип пути и превышение лимита блокируют копирование, resume и acknowledgement. Для невалидного журнала runtime не угадывает slot-имена или artifact manifest; Editor показывает категорию и исходную диагностику, сообщает, что файлы не менялись, и просит проверить журнал/каталог вручную. Integration test покрывает будущую версию, усечение, превышение лимита и каталог на месте journal; проверяет неизменность candidate save и отказ всех операций. Editor smoke аналогично сохраняет malformed journal и похожий на slot файл. Это не восстановление/очистка журнала и не проверка происхождения файлов. См. [06.43 report](reports/06.43-invalid-campaign-transfer-journal-diagnostics.md) и [D105](DECISIONS.md#d105--diagnose-invalid-campaign-transfer-journals-0643).

## 06.44 — Подтверждённый архив читаемого malformed-журнала (поведение уточнено в 06.45)

В 06.44 появился путь подтверждённого архивирования; проверка 06.45 убрала небезопасное удаление оригинала. Текущее поведение описано в [06.45](reports/06.45-race-safe-malformed-journal-archive.md) и [D107](DECISIONS.md#d107--preserve-malformed-journals-during-archive-0645).

## 06.45 — Архивная копия без гонки удаления malformed-журнала (implemented)

Для читаемого ограниченного `Malformed` журнала Editor показывает отдельное подтверждение создания архивной копии. Runtime эксклюзивно создаёт соседний файл с точными байтами и проверяет его; оригинал намеренно остаётся на месте и продолжает блокировать переносы до ручной проверки. Повторная операция принимает только идентичную архивную копию; коллизия с другими байтами не перезаписывается. В 06.48 добавлена lifecycle-блокировка исходного журнала на время архивирования и проверены отказы при конкурирующем Windows/POSIX lock. Блокировка не меняет политику сохранения исходника: удаление malformed-журнала не реализовано. Слоты сохранений не читаются и не меняются. См. [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md), [06.48 report](reports/06.48-malformed-journal-archive-lock.md) и [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## 06.46 — Безопасное подтверждение корректного recovery-журнала (implemented with platform limits)

Windows открывает проверенный журнал через `CreateFileW` с `GENERIC_READ | DELETE` и share mode только для чтения, проверяет точные байты через этот handle и удаляет тот же file object через `SetFileInformationByHandle`. Если конкурирующий writer или filesystem мешает получить handle/удалить, операция отказывает и оставляет запись. На POSIX API пока fail-closed; будущий backend требует `O_NOFOLLOW`, advisory lock, проверки inode через открытый directory fd и `unlinkat`. Advisory lock действует только для сотрудничающих процессов, поэтому POSIX-поддержка остаётся выключенной до отдельной платформенной сборки и race-тестов. Integration тесты Windows проверяют устаревший журнал, удерживаемый writer handle, успешное подтверждение и сохранность слотов. См. [06.46 report](reports/06.46-handle-based-campaign-journal-acknowledgement.md) и [D108](DECISIONS.md#d108--handle-based-valid-recovery-journal-acknowledgement-0646).

## 06.47 — POSIX acknowledgement и блокировка журнала на время транзакций (implemented)

Подтверждение recovery-журнала теперь работает на Windows и POSIX. Copy и resume удерживают lease одного и того же журнала на всём протяжении транзакции; acknowledgement сначала сверяет показанный preview и список артефактов, затем берёт ту же блокировку, повторно сравнивает точные байты и удаляет только journal entry/file object. Windows использует `LockFileEx` плюс удаление открытого объекта через `SetFileInformationByHandle`; POSIX открывает каталог и файл без следования symlink, берёт `flock`, сверяет `fstat`/`fstatat(AT_SYMLINK_NOFOLLOW)` и вызывает `unlinkat`. POSIX advisory lock координирует только процессы, соблюдающие протокол; произвольный локальный процесс может его игнорировать. Запись игровых слотов acknowledgement не изменяет. Проверены отдельный блокирующий процесс на POSIX, конкурирующая блокировка Windows и отказ во время активного copy. Ubuntu 24.04 под WSL2 использует сборку в Linux filesystem. Финальные проверки: Windows Release CTest 52/52; POSIX Release CTest 39/39. См. [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md), [D110](DECISIONS.md#d110--cross-platform-campaign-journal-lifecycle-lock-0647) и историческое обоснование readiness в [D109](DECISIONS.md#d109--gate-posix-journal-acknowledgement-on-a-runnable-target-0647-assessment).

## 06.48 — Lifecycle lock для архивирования malformed-журнала (implemented)

Архивирование сначала валидирует displayed preview, затем получает lease исходного bounded malformed-журнала, сравнивает его точные байты через этот handle и держит lease до верификации соседней копии. Если transfer/archive lock уже занят, операция отказывает до создания архива. Оригинал намеренно сохраняется; слоты не затрагиваются. Тест покрывает конкурентный `LockFileEx` на Windows и удержание POSIX `flock` отдельным процессом, после освобождения операция проходит. Полный Release CTest: Windows 52/52, WSL Ubuntu 24.04 39/39. POSIX advisory-lock ограничение остаётся. См. [06.48 report](reports/06.48-malformed-journal-archive-lock.md) и [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## 06.49 — Оценка восстановления частичной архивной копии (assessment, superseded by 06.50)

Архив создавался эксклюзивно сразу под итоговым детерминированным именем. Анализ и fixture-тест обнаружили риск частичного финального файла; это поведение исправлено в 06.50. Историческая оценка и воспроизводимый сценарий: [06.49 report](reports/06.49-partial-journal-archive-assessment.md), [D112](DECISIONS.md#d112--preserve-partial-journal-archive-residue-0649).

## 06.50 — Staged atomic no-replace archive publication (implemented)

Malformed-журнал по-прежнему сохраняется и удерживается под прежним lifecycle lease. Архив полностью записывается и flush-ится под уникальным именем рядом с итоговым файлом, повторно сверяется через удерживаемый handle и публикуется без замены: Windows использует handle-based `FileRenameInfo` с `ReplaceIfExists=FALSE`, POSIX — `linkat` и последующее удаление staging entry. Stale staging-файлы после аварийного завершения сохраняются для ручной проверки, но не блокируют новую попытку. Существующий неполный финальный архив всё ещё вызывает отказ, его нельзя автоматически исправить. Integration-тест проверяет сохранность stale staging occupant и конфликт финального пути; это не реальный crash/power-loss тест. Результаты: [06.50 report](reports/06.50-staged-journal-archive.md), [D113](DECISIONS.md#d113--staged-atomic-no-replace-journal-archive-publication-0650).

## 06.51 — Проверка прерывания staged-архива (implemented)

Integration-тест запускает дочерний тестовый процесс и принудительно завершает его после частичной записи, после exact-byte проверки до публикации и после публикации до очистки staging. Для каждого состояния проверяются итоговое имя, остаток staging, последующий retry, неизменность malformed-журнала и контрольного slot-файла. Seam доступен только через внутренний тестовый заголовок и не добавляет управляющую переменную или параметр в production API. Windows Release: focused 1/1, full CTest 40/40; WSL Ubuntu Release: focused 1/1, full CTest 39/39. Это проверка завершения процесса, не питания/ядра/файловой системы. Подробности: [06.51 report](reports/06.51-archive-interruption-tests.md), [D114](DECISIONS.md#d114--subprocess-interruption-tests-for-staged-journal-archive-0651).

## 06.52 — Оценка ручного разбора конфликтного архива (assessment)

Архив с точными байтами проверенного журнала можно безопасно повторно использовать. Отличающийся файл по ожидаемому пути нельзя надёжно признать остатком старой попытки: он может быть прежним частичным архивом 06.49, пользовательским файлом, изменённым архивом или коллизией 64-битного fingerprint. Совпадение префикса также не подтверждает происхождение. Текущий fail-closed отказ сохраняется. Рекомендация для отдельной реализации: read-only диагностика с точным путём, типом, размером, доступностью и результатом сравнения (точный/отличающийся/префикс/недоступный), без автоматического удаления, перемещения или замены. Пользователь вручную разбирает неизвестный объект и повторяет операцию. Код и поведение продукта не менялись; полный baseline CTest до оценки: Windows 40/40, WSL Ubuntu 39/39. См. [06.52 report](reports/06.52-legacy-archive-conflict-assessment.md) и [D115](DECISIONS.md#d115--manual-review-of-legacy-conflicting-archive-files-0652).

## 06.53 — Read-only диагностика конфликта архива (implemented)

Runtime предоставляет инспекцию детерминированного архивного пути только для показанного пользователю, читаемого malformed-журнала. Она классифицирует отсутствующий путь, обычный файл, каталог, ссылку/reparse point, другой тип или недоступный объект. Содержимое читается только у обычного файла и ограничено 4096 байтами; результат — точное совпадение, отличие, префикс, превышение лимита или недоступность. Проверка работает с no-follow handle и сверяет стабильность объекта; исходный журнал дополнительно удерживается под lease. Editor показывает путь, тип, размер, сравнение и диагностику после отказа архивирования. Smoke и integration-тесты проверяют exact/different/prefix/oversized, каталог и ссылку, сохранение байтов журнала, слота и владельца архива. Windows Release полный CTest 44/44; WSL Ubuntu Release 39/39. Это сравнение байтов, не подтверждение владельца; файл не исправляется и не заменяется. См. [06.53 report](reports/06.53-archive-conflict-diagnostics.md), [D116](DECISIONS.md#d116--read-only-diagnostics-for-archive-conflicts-0653).

## 06.54 — Оценка явного сохранения конфликтующего файла копией (assessment)

Рассмотрен только будущий пользовательский workflow. Возможный кандидат: отдельное подтверждение копии читаемого обычного файла размером до 4096 байт в новую соседнюю цель с уникальным именем и публикацией без замены. Источник удерживается и повторно проверяется во время bounded exact-byte копирования; оригинал сохраняется. Ссылка/reparse point, каталог, другой тип, недоступный или oversized объект не копируется. Диалог должен показывать источник, предложенное назначение, размер и сообщение, что авторство неизвестно. Успешная копия не проверена как архив и не очищает исходный конфликт; ручная проверка остаётся обязательной. 06.54 изменил только документы; никакой код, формат, API или данные не менялись. Перед правками Windows Release Editor build завершился (154/154 задач, без ошибок компиляции; были предупреждения C4189), WSL Ubuntu build сообщил `ninja: no work to do`. CTest до документальных изменений: Windows 44/44 (14.95 s), WSL Ubuntu 39/39 (0.80 s). См. [06.54 report](reports/06.54-conflict-copy-assessment.md) и [D117](DECISIONS.md#d117--bounded-explicit-copy-of-unknown-archive-occupants-0654-assessment). Следующий шаг — реализация только после отдельного подтверждения.

## 06.55 — Подтверждаемое сохранение копии конфликтующего файла (implemented)

Runtime preview разрешает только отличающийся bounded regular file до 4096 байт и выбирает свободное соседнее имя. Подтверждение удерживает no-follow source handle, повторно проверяет identity/bytes и malformed journal, пишет уникальный staging-файл и публикует его атомарно без замены. Editor показывает оба пути, размер и предупреждение, что файл не подтверждён и его автор неизвестен; cancel не пишет файлы. Источник, журнал и slot не меняются, копия не очищает исходный конфликт. Тесты покрывают отказ без подтверждения, destination race, stale source, oversized/directory/symlink и процессное завершение до/после публикации. Полный Release CTest: Windows 44/44, WSL Ubuntu 39/39. Ограничения: копия не валидируется как формат журнала; interruption/power loss и hostile local writers за пределами проверенных сценариев; UI диалог проверен Editor smoke, не ручным кликом. См. [06.55 report](reports/06.55-conflict-copy.md), [D118](DECISIONS.md#d118--explicit-bounded-copy-aside-for-archive-conflicts-0655).

## 06.56 — Unicode-путь и ручная инспекция диалога (implemented)

Integration добавляет Unicode-каталог `Проверка копии Ω` и проверяет точные байты в созданной копии, исходном конфликте, журнале и save slot на Windows и WSL/POSIX. Добавлен тестовый режим `--smoke-inspect-copy-dialog`: он открывает встроенный подтверждающий диалог на временной smoke-фикстуре, ждёт ручного действия и при отмене проверяет отсутствие копии и сохранность трёх исходных файлов. Диалог визуально проверен; отмена завершилась кодом 0. Windows Release полный CTest 44/44; WSL Ubuntu Release 39/39. Это проверка двух текущих файловых систем; другие POSIX/macOS/mobile платформы не проверялись. Никакой пользовательский save data не открывался. Подробности: [06.56 report](reports/06.56-unicode-copy-and-dialog-review.md), [D119](DECISIONS.md#d119--unicode-path-and-manual-dialog-verification-0656).

## 06.57 — Оценка переносимости copy-aside на macOS/POSIX (assessment)

Проверены исходный POSIX путь и документация Apple. Используемые descriptor-relative вызовы и no-follow флаги документированы Apple; advisory `flock` годится только для сотрудничающих процессов, а same-filesystem hard link поддерживает атомарное создание новой записи без замены. Критический незакрытый вопрос — durability: обычный macOS `fsync` не гарантирует физическую запись до потери питания, `F_FULLFSYNC` — специфичное Apple расширение, а поведение каталожного `fsync` и фактические гарантии APFS здесь не испытаны. Linux WSL не подтверждает macOS. Код/API не менялись. Windows Release build и полный CTest 44/44, WSL Ubuntu Release build без работы и полный CTest 39/39. См. [06.57 report](reports/06.57-posix-macos-portability-assessment.md), [D120](DECISIONS.md#d120--macos-posix-copy-aside-portability-assessment-0657). Следующий рекомендуемый шаг: macOS CI/host validation этого POSIX transaction на APFS с тестами publication, directory flush outcomes, Unicode, lock contention и interruption; не объявлять durable power-loss safety.

## 06.58 — исправление переносимости тестовой сборки по результату Mac

Пользовательский Mac-прогон выявил неоднозначный `std::to_string` в `CSharpCodeWorkspaceTests.cpp` и `CSharpBuildManifestTests.cpp`. В обоих тестах счётчик `file_time_type` теперь явно приводится к `long long` перед форматированием. Локально Release build и тесты прошли на Windows (focused 2/2, full 44/44) и WSL Ubuntu (focused 2/2, full 39/39). Требуется повторный build/CTest на Mac: локальные результаты не подтверждают macOS. Подэтап ограничен тестовой переносимостью; файловый durability/API путь не менялся. См. [06.58 report](reports/06.58-macos-test-portability-fix.md), [D121](DECISIONS.md#d121--explicit-time-count-conversion-in-cross-platform-tests-0658).

00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.10 и 06.1–06.4: см. отчёты подэтапов.

Выполняется только явно назначенный пользователем подэтап. После него — отчёт и остановка до ручного подтверждения. Стек остаётся предварительным до успешной проверки 00.3; шаблоны создаются после реализации необходимых подсистем.

Нумерация этапов разработки 00–10 отличается от нумерации разделов требований 01–10. Упоминание «раздела 08» ниже относится к каталогу шаблонов GAME_TEMPLATES.md.

Каждый этап также подчиняется [TESTING.md](TESTING.md): unit/integration/smoke/regression-проверки, специальные проверки графики, производительности, сохранений и безопасности, а также документация, пример, тест и известные ограничения для каждой готовой функции.

## ЭТАП 00 — ОСНОВА РЕПОЗИТОРИЯ И ПРОВЕРКА СТЕКА

- **00.1** Создать структуру каталогов, README, ROADMAP, STATUS, ARCHITECTURE,
     DECISIONS, TESTING, .gitignore, CMakeLists.txt, минимальный app и тест.
     Приёмка: чистая конфигурация CMake, сборка и CTest; инструкции Windows.
- **00.2** CI/локальные скрипты проверки, форматирование, статический анализ
     по возможности, логирование и обработка ошибок.
     Приёмка: воспроизводимый отчёт о сборке и тестах.
- **00.3** Spike SDL3+bgfx+ImGui: окно, треугольник/примитив, ввод, docking.
     Приёмка: запускаемый прототип и документ о совместимости/лицензиях;
     альтернативы при сбое. Не объявлять стек утверждённым раньше.

## ЭТАП 01 — ЯДРО БЕЗ GUI

- **01.1** EngineContext, Result/Error, Logger, конфигурация.
- **01.2** ModuleRegistry, жизненный цикл, зависимости, ошибки.
- **01.3** EventBus, TimeService, fixed timestep.
- **01.4** CommandRegistry, schema, validation, execute/result.
- **01.5** Транзакции, Undo/Redo и тесты; документировать необратимые действия.

Приёмка: headless executable, тесты порядка запуска, событий, команд,
отмены, ошибочных входных данных и корректного завершения.

## ЭТАП 02 — МИНИМАЛЬНЫЙ РЕДАКТОР

- **02.1** Окно и docking layout; меню и проект.
- **02.2** Scene Model, Scene View, Hierarchy.
- **02.3** Inspector, schema properties, изменение через Command API.
- **02.4** Сохранение/открытие сцен, диагностика, Undo/Redo, hotkeys.

Приёмка: пользователь создаёт сцену и объект, меняет свойство,
сохраняет, перезапускает редактор, открывает сцену, отменяет действие.

## ЭТАП 03 — КОМПОНЕНТЫ, РЕСУРСЫ, ПРЕФАБЫ

- **03.1** Entity/Component IDs, Transform, базовая сериализация и миграции.
- **03.2** Asset Registry, импорт 2D-ресурсов, ссылки, кэш и переимпорт.
- **03.3** SpriteRenderer и отображение сцены; missing assets diagnostics.
- **03.4** Префабы, nested prefab, overrides, тест обновления экземпляров.
- **03.5** Пайплайн платформенных производных ресурсов и зависимости.

Приёмка: замена текстуры не ломает логику/ссылки; изменения префаба
применяются предсказуемо; неизвестные компоненты диагностируются.

## ЭТАП 04 — ИГРОВОЙ РЕЖИМ

- **04.1** Play/Stop/Pause/Step, изоляция состояния от редактора.
- **04.2** Input Mapping, клавиатура/мышь/геймпад.
- **04.3** Камеры и следование, переключение, параметры.
- **04.4** Runtime events и базовые интерактивные компоненты.
- **04.5** Технический C# scripting prototype: Windows x64 native `hostfxr` host загружает managed component; fixed-step integration проверяет изменение runtime Scene copy и изоляцию authoring Scene при Stop. Проверено с локально распакованным .NET 10 SDK 10.0.404. Host остаётся experimental/optional и не является финальным выбором; Editor, полноценные bindings, sandbox, hot reload, mobile strategy и shipping/package tests остаются вне подэтапа. См. [отчёт 04.5](reports/04.5-csharp-runtime-prototype.md).
- **04.6** Реализован дополнительный технический шаг: versioned C# ABI v1 для fixed-step и ограниченных Transform callbacks. Это добавление к мастер-плану (исходный раздел 10 не содержал отдельного 04.6); см. [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md) и [отчёт 04.6](reports/04.6-versioned-csharp-runtime-api.md).
- **04.7** Реализован дополнительный технический шаг: persistent `ScriptBehavior` binding в Astro Scene v5 с AssetId, managed type name, enabled/schema fields, чтением v1–v4 и round-trip v5. Lifecycle описан как контракт; runtime dispatch, asset resolver и Editor authoring не входят в этот шаг. Это добавление к мастер-плану; см. [отчёт 04.7](reports/04.7-persistent-script-behavior.md).
- **04.8** Реализован дополнительный технический шаг: общий Asset index v2 с глобально уникальными AssetId для image и C# scripts; сохранение источника, type, assembly/runtimeconfig paths; проверяемое разрешение ID через optional Windows hostfxr adapter. Registry не компилирует код и не исполняет lifecycle; Editor authoring и sandbox не добавлены. См. [C# scripting API](CSHARP_SCRIPTING_API.md) и [отчёт 04.8](reports/04.8-project-script-asset-registry.md).
- **04.9** Реализован дополнительный технический шаг: локальный C# build manifest и CLI с точным pin .NET SDK, offline restore, детерминированной компиляцией, immutable output generations, проверкой source/build/output hashes и stale diagnostics. Только `Scripts/**/*.cs`; CLI не связывает компонент с RuntimeSession, не исполняет код и не является sandbox. Это добавление к мастер-плану; см. [C# scripting API](CSHARP_SCRIPTING_API.md), [FILE_FORMATS](FILE_FORMATS.md) и [отчёт 04.9](reports/04.9-csharp-project-build.md).
- **04.10** Реализован дополнительный шаг: `scriptBindings` в build manifest, versioned C++ handoff, AssetRegistry freshness/status и явная атомарная синхронизация Script AssetIds; Project panel показывает сборочные статусы/диагностики и действие регистрации. Наличие managed type не проверяется до явного host load; автоматический lifecycle и Code workspace не добавлены. См. [отчёт 04.10](reports/04.10-csharp-build-registry-diagnostics.md).
- **04.11** Определена политика разрешения на будущий Play-запуск C#: один интерактивный opt-in на точный набор ScriptBehavior и build generation, без сохранения доверия и без AI auto-approval. Это проектное решение, не исполняемый gate; UI/runtime интеграция остаются отдельным шагом. См. D077 и [отчёт 04.11](reports/04.11-csharp-play-approval-policy.md).
- **04.12** Завершён Editor consent gate для точного набора C# bindings/build hashes, с повторной проверкой на подтверждении и односессионным разрешением.
- **04.13** Завершено подключение enabled ScriptBehavior к RuntimeSession lifecycle с повторной проверкой consent перед каждой уникальной host-загрузкой; добавлены тесты и документация ABI.
- **04.14** Завершён Inspector authoring зарегистрированных C# bindings с транзакционными add/enable/remove, сохранением Scene v5 и Undo/Redo.
- **04.15** Добавлена базовая C# Code workspace: открытие/создание/сохранение UTF-8 `.cs` только внутри `Scripts`, unsaved-change prompt, явный запуск существующей сборки, compiler diagnostics с переходом к файлу/строке. Это ограниченный Windows Editor flow без syntax highlighting, LSP, debugger или sandbox. См. [отчёт 04.15](reports/04.15-csharp-code-workspace.md).
- **04.16** Добавлена форма project build settings для `.astro/csharp-build.json`: SDK pin, фиксированный `net10.0`, assembly name, source/type bindings. Строгие schema/path checks и атомарная запись; повреждённый манифест требует явного подтверждения замены. Build pipeline читает этот манифест. См. [отчёт 04.16](reports/04.16-csharp-build-manifest-editor.md).
- **04.17** Project C# report различает отсутствующую регистрацию, расхождение managed type, source hash и assembly outputs. Несовпадение исправляется явной синхронизацией текущего build; Script AssetId сохраняется. Это проверка manifest/build/registry, не CLR reflection: существование типа подтверждается только при загрузке в одобренной Play-сессии. См. [отчёт 04.17](reports/04.17-csharp-binding-diagnostics.md).

Приёмка: интерактивная сцена управляется и не портит исходную сцену.

## ЭТАП 05 — ПЕРВАЯ ЗАКОНЧЕННАЯ 2D-ИГРА

- **05.1** Минимальная 2D-физика/коллизии и события.
- **05.2** Game UI: меню, HUD, пауза, настройки.
- **05.3** Звук, анимации, игровая логика головоломки.
- **05.4** Save System v1: выбор сохраняемых полей, слоты, load/save,
     версионирование, защита от повреждённой записи.
- **05.5** Checkpoint v1: триггер и респаун, выбранные группы данных.
- **05.6** Windows runtime packaging; smoke-тест чистого запуска.
- **05.7** Подключение визуальной Puzzle-доски и ходов к Gameplay-экрану runtime demo.
- **05.8** Загрузка авторской Puzzle-кампании, fixed-step ходы, переходы и terminal END в standalone runtime demo.
- **05.9** Явное ручное сохранение/восстановление выбранного прогресса авторской кампании в standalone runtime demo.
- **05.10** Самодостаточный Windows runtime ZIP с примером кампании, относительным launcher и smoke сохранения/восстановления из распаковки.

Приёмка: 2D-головоломка с несколькими уровнями, меню, звуком,
сохранением прогресса и самостоятельным Windows-приложением. Эта приёмка
пока не выполнена: 05.9 добавляет ручное сохранение campaign progress, а 05.10
включает один пример кампании в ZIP, но это не завершает весь игровой цикл и
экспорт произвольного проекта. 06.31 добавляет ручные campaign slots в Editor
Game View; автосохранение и миграции отсутствуют. Новый ZIP проверен из чистой распаковки;
это обновляет demo-артефакт, но не завершает самостоятельный экспорт игры.

## ЭТАП 06 — NO-CODE И РЕДАКТОРЫ

Подэтап 06.1 реализован ограниченно: typed pure-dataflow prototype и базовая панель Visual Logic. Граф пока не сохраняется, не исполняет команды и не меняет Scene; отсутствуют flow-control и drag-wire authoring. Это не приёмка этапа 06.

Подэтап 06.2 реализован ограниченно: in-memory UI document authoring, две встроенные темы, Desktop/Tablet/Phone responsive preview и две валидируемые Scene bindings. Документ не сохраняется и не используется runtime Game UI; полная приёмка этапа 06 не достигнута.

Подэтап 06.3 реализован ограниченно: scalar timeline/keyframe authoring and preview playback, plus CameraRig presets/custom pose panel. Neither timeline values bind to Scene properties nor camera output is rendered; full Stage 06 acceptance remains open.

Подэтап 06.4 реализован ограниченно: in-memory explicit profile fields and entity groups, checkpoint authoring/XY preview, plus an isolated SaveStore/CheckpointSystem restore preview. Project persistence and unsupported gameplay properties remain open; see [SAVE_SYSTEM_EDITOR.md](SAVE_SYSTEM_EDITOR.md) and [report](reports/06.4.md).

Перед шаблонами 06.5 добавлена ограниченная prerequisite-основа: Canvas2D, SDL3 renderer, affine-композиция иерархии и standalone project Scene preview из существующего Scene-файла/AssetRegistry с безопасной проверкой путей. WIC texture cache остаётся Windows-only. Editor Play workflow, level/progress persistence и сами шаблоны не реализованы. См. [RENDERER_2D.md](RENDERER_2D.md) и [отчёт](reports/06.5-hierarchy-project-preview.md).

Для T01 Puzzle добавлены in-memory `PuzzleSnapshot`, явно выбранные custom values в SaveStore v2 и optional adapter `astro_runtime_puzzle_save` с версионированным slot round-trip и транзакционной загрузкой Scene. Подэтап 06.15 добавил встроенный playable preview в Game View с соседними tile swaps и fixed-step runtime smoke. Это всё ещё не T01 project template: authoring уровней и настройка SaveProfile/checkpoint для Puzzle отсутствуют. См. отчёты [SaveStore custom fields](reports/06.6-save-custom-fields.md), [Puzzle save adapter](reports/06.7-puzzle-save-adapter.md) и [Game View](reports/06.15-puzzle-game-view.md).

## T01 Puzzle — ручные слоты в Game View (06.16)

Puzzle preview подключён к `astro_runtime_puzzle_save`: Game View предлагает слоты 0–9 и ручные Save/Load, сохраняя только явно выбранный `puzzle.snapshot`. Обычный Editor использует SDL user-preferences directory; smoke использует удаляемую тестовую папку. Это не профильная система авторинга или шаблон: путь и поле для sample mode пока фиксированы. Результаты: [отчёт 06.16](reports/06.16-puzzle-save-game-view.md).

## T01 Puzzle — project-scoped профиль и каталог прогресса (06.17)

Game View позволяет настраивать SaveProfile ID/schema и user-data relative subdirectory в per-project `.astro/puzzle-save.settings`. Формат `project.astro` v1 не менялся. Новые проекты получают разные default subdirectories; изменённая подпапка и профиль применяются только после `Apply project settings`. Smoke проверяет round-trip, path validation, project isolation и сохранение/загрузку Puzzle state. Старые глобальные слоты не перемещаются; их можно открыть, выбрав прежнюю подпапку вручную. Результаты: [отчёт 06.17](reports/06.17-project-puzzle-save-settings.md).

## T01 Puzzle — авторинг и отдельный файл уровня (06.18)

Game View содержит первую панель Puzzle Level Authoring: редактирование размеров/параметров и стартовых клеток, загрузка/сохранение файла уровня под `Puzzle/Levels`, запуск выбранного draft. Runtime `PuzzleGame` получает отдельный overload `create(PuzzleLevelDefinition)`; старый API генерации по seed сохранён. Format v1 валидирует ID, UTF-8 имя, bounds и tile IDs. Authoring slots отключены, так как SaveStore не связывает payload с level ID. Unit, file integration и Editor smoke проверяют создание/загрузку и запуск уровня. Это первый ограниченный авторский шаг, не полный T01 template, список уровней или прогресс кампании. См. [гайд](PUZZLE_LEVELS.md) и [отчёт 06.18](reports/06.18-puzzle-level-authoring.md).

- **06.1** Visual Scripting, типизированные узлы и отладка.
- **06.2** UI Editor, темы, привязки, responsive layout.
- **06.3** Timeline/анимации, пресеты камер.
- **06.4** Save System Editor: профили, группы, визуальная расстановка
     checkpoint, ручное тестирование восстановления.
- **06.5** Библиотека первых 2D-шаблонов: Puzzle, Platformer, Top-Down.
- **06.6** Расширение C# Code workspace: UX с управляемыми файлами/manifest, улучшенная навигация и диагностика, интеграция Inspector bindings/runtime с Editor smoke; не обещать IDE-сервисы или sandbox без отдельной реализации и оценки.

Приёмка: новая механика собирается в GUI без написания C++.

## ЭТАП 07 — 3D И РЕДАКТОР МИРОВ

- **07.1** 3D meshes, glTF, 3D камеры, 3D физика. 07.1a добавил backend-neutral camera/mesh контракт и опциональный bgfx cube smoke. 07.1b реализовал отдельный opt-in ограниченный glTF/GLB importer/cache для static triangle POSITION геометрии; зависимости локальны и проверяются по commit/hash, скрытая загрузка CMake отключена. Windows Release импортной конфигурации собрался, CTest 42/42; стандартная headless Release конфигурация также собирается без импортёра. Scene/AssetRegistry/Editor integration, материалы/текстуры, нормали/UV, node transforms, анимация и physics остаются отдельными будущими частями. Нужны fuzz/sanitizer и проверки на macOS/mobile до более широких заявлений. См. [07.1 assessment](reports/07.1-3d-foundation-assessment.md), [07.1b report](reports/07.1b-bounded-gltf-importer.md), [D122](DECISIONS.md#d122--keep-3d-prototypes-modular-before-dependency-adoption-071-assessment), [D125](DECISIONS.md#d125--optional-bounded-gltf-static-mesh-importer-071b).
- **07.2** Material Editor, PBR, освещение, тени.

В 07.2a–07.2e принят поэтапный data-first путь: контракт материалов; `ASTRO_MATERIAL`/AssetRegistry; импорт bounded glTF material metadata/source slots; Scene v7 с per-primitive project override и transactional command; Inspector с зарегистрированными материалами и ссылочной диагностикой. Параметры материала, текстуры/GPU, PBR shader, lighting и shadows остаются следующими отдельными задачами, а не подразумеваются готовыми.

**07.2f — первый ограниченный material draw prototype (completed):** standalone Windows BGFX smoke разрешает Scene material override и применяет только `baseColorFactor` как плоский tint к существующему vertex color; screenshot проверяет краснодоминантную область. Это не Editor/Game View renderer и не PBR. Геометрический контракт остаётся POSITION + triangle indices; normals/UV glTF по-прежнему отвергаются, texture roles не загружаются. Подробности — [3D Rendering](3D_RENDERING.md#first-bgfx-material-draw-path-072f), [Testing](TESTING.md#072f-first-bgfx-material-draw-path) и D138.

**07.2g — нормали и UV0 в bounded mesh/glTF import contract (completed):** `MeshData` хранит optional NORMAL и TEXCOORD_0 с явными флагами; importer валидирует форму, компонентный тип, нормализацию, count, границы и конечность данных, а derived cache v2 сохраняет каналы отдельно от v1. Integration/unit regressions проверяют float и normalized integer UV, кэш и некорректные accessor-ы. Это data/import slice; шейдеры, texture upload, PBR и Editor не подключены. Результаты в [Testing](TESTING.md#072g-normals-and-uv0-mesh-import), [отчёте](reports/07.2g-normal-uv-mesh-import.md) и D139.

**07.2h — base-color texture prototype (completed):** shared bounded Windows WIC decoding reads an AssetRegistry cached PNG; the standalone BGFX D3D11 smoke resolves a `BASE_COLOR` binding to UV0 and verifies four broad sampled color families from screenshot readback. It remains separate from Editor/Game View, does not multiply `baseColorFactor`, and does not perform sRGB color management or PBR. See [testing](TESTING.md#072h-base-color-texture-smoke), [report](reports/07.2h-safe-base-color-texture-smoke.md), and D140.

**07.2i — sRGB base-color and linear factor contract (completed prototype):** source RGB is decoded with the IEC sRGB transfer curve, multiplied by the linear material factor, quantized into a temporary linear BGRA8 upload buffer, then displayed through an sRGB backbuffer. Unit tests check transfer and validation; a separate BGFX D3D11 smoke checks expected encoded values and retains 07.2h's texture-only smoke as regression. This is a bounded CPU preparation prototype, not a production GPU material pipeline. See [testing](TESTING.md#072i-base-color-color-space-and-factor), [report](reports/07.2i-base-color-color-space.md), and D141.

**07.2j–07.2r завершены как ограниченные прототипы:** Scene View отображает base-color проекта и glTF source material по правилу `project override > source material > vertex color`; Inspector указывает активный источник, объясняет переход к source через сброс override и перечисляет реальные возможности preview. Authoring smoke назначает/снимает override через typed command. Неразрешимый явный override остаётся диагностируемым vertex-color fallback. 07.2n подключил wrap/filter и `KHR_texture_transform` для source glTF base-color на UV0, 07.2o добавил sampler/transform authoring для проекта, 07.2p — mip-цепочки и общий кэш, 07.2r — OPAQUE/MASK/BLEND и порядок draw items только в Editor Scene View. MASK/BLEND временно не используют mip-цепочки; Game View, остальные PBR роли, lighting и shadows не реализованы.

**07.2m — оценка glTF sampler и texture-coordinate transform (completed assessment):** спецификация и текущий BGFX/Editor путь сопоставлены без изменений кода или формата. Результаты оценки и тестовая матрица — в [отчёте 07.2m](reports/07.2m-gltf-sampler-and-uv-transform-assessment.md) и D145.

**07.2n — source glTF base-color sampler и UV transform (completed prototype):** importer preserves sampler settings and transform metadata; Scene View applies wrap modes, nearest/linear for one-mip textures, and offset/rotation/scale through a GPU-only UV copy. Mip filters use a visible vertex-color fallback.

**07.2p — bounded base-color mip chains and shared Scene View texture cache (completed prototype):** source and project base-color textures can use all six minification modes. Mips are generated from linearized image data; the Scene View cache shares identical GPU uploads and releases them after the last mesh reference. Limits: 16 Mi source pixels, 96 MiB per chain, 256 MiB live cache. See [D148](DECISIONS.md#d148--bounded-base-color-mips-and-shared-texture-cache-072p), [tests](TESTING.md#072p-bounded-base-color-mip-chains-and-shared-cache), [3D Rendering](3D_RENDERING.md#bounded-base-color-mip-chain-preview-072p), and [report](reports/07.2p-bounded-mip-chains.md).

**07.2q — Scene View alpha mode and depth order (completed assessment):** existing alpha fields/import metadata and pass policy were assessed before implementation. See [D149](DECISIONS.md#d149--scene-view-alpha-mode-and-depth-order-assessment-072q) and [07.2q report](reports/07.2q-alpha-blending-depth-assessment.md).

**07.2r — bounded Scene View alpha pass (completed prototype):** Windows D3D11 Scene View now compiles owned OPAQUE/MASK/BLEND fragment shaders from source, submits explicit depth/blend states and stable object-level queues, and honors `doubleSided`. Unit sorting and Editor screenshot smoke are covered; alpha-aware mipmaps, cross-platform parity, Game View, intersecting transparent triangles, OIT and broader PBR are still open. See [D150](DECISIONS.md#d150--bounded-scene-view-alpha-pass-072r), [tests](TESTING.md#072r-scene-view-alpha-pass), [3D Rendering](3D_RENDERING.md#scene-view-alpha-pass-072r), and [report](reports/07.2r-scene-view-alpha-pass.md). The next 07.2 substage needs a new review and explicit user instruction.
- **07.3** Terrain Editor, растительность, дороги.
- **07.4** Water System, VFX/частицы, постобработка.
- **07.5** LOD, instancing, streaming, budgets и профили качества.

Приёмка: редактируемая 3D-сцена с измеренной производительностью.

## ЭТАП 08 — ПРОДВИНУТЫЕ МЕХАНИКИ И ШАБЛОНЫ

- **08.1** First Person Controller, Third Person Controller.
- **08.2** FPS Shooter: оружие, боеприпасы, враги, HUD, checkpoint.
- **08.3** Racing: транспорт, колёса, подвеска, трасса, таймер.
- **08.4** NPC, navigation, behavior, интерактивные объекты.
- **08.5** Шаблоны Isometric, 3D Adventure, Strategy и прочие из раздела 08.
- **08.6** Большие миры и расширенная транспортная система, включая прицепы.

Приёмка: каждый шаблон запускается, имеет инструкции и smoke-тесты.

## ЭТАП 09 — AI-FIRST РЕДАКТОР И ТЕСТЕР

- **09.1** Локальный AI Editor API, перечень команд и схемы — реализован базовый in-process C++ фасад 1.0 и каталог зарегистрированных Scene-команд; transport/JSON/permissions не входят в эту поставку. См. [отчёт 09.1](reports/09.1.md).
- **09.2** Первый локальный Codex adapter: read-only контекст активной загруженной сцены и проверка описательного плана по реально зарегистрированным командам реализованы. Полное дерево проекта, транспорт, payload/diff и исполнение не входят в этот срез; см. [отчёт 09.2](reports/09.2.md).
- **09.3** В работе: 09.3a–09.3g реализуют узкие AI review потоки; 09.3h оценил пробел Transform API; 09.3i добавил транзакционную typed-команду `entity.set_transform` v1 для полного локального Transform с Scene-валидацией и Undo/Redo. AI preview/UI для Transform, общий command approval, разрешения, checkpoints и rollback остаются незавершёнными. Следующий шаг — отдельный AI Change Review preview поверх новой команды, с явным local before/after и вычисляемой мировой позой камеры только для чтения. См. [отчёт 09.3i](reports/09.3i-transform-command.md).
- **09.4** AI Game Tester: сценарии, логи, регрессии, отчёты.
- **09.5** AI-помощь в генерации шаблонов и оптимизации с валидацией.

Приёмка: Codex по текстовому запросу меняет сцену через API,
показывает изменения, пользователь принимает/отклоняет, тесты проходят.

## ЭТАП 10 — ПОРТИРОВАНИЕ И ВЫПУСК

- **10.1** macOS: проверка сборки, ввод, ресурсы, упаковка.
- **10.2** Android: ввод/касания, жизненный цикл, производительность, экспорт.
- **10.3** iOS: toolchain, подпись/сборка на совместимом Mac, ввод, упаковка.
- **10.4** Документация, примеры, миграции, релизные тесты.

Приёмка: подтверждённые сборки на реальных целевых toolchains;
Windows не должен ломаться. Не обещать iOS-сборку на Windows без Mac.

02.1–03.5, 04.1–04.4 и 05.1–05.6 реализованы в пределах ограничений [отчётов](reports/). Следующий шаг не нумерован в текущем плане: требуется отдельно определить работу для недостающей визуальной puzzle/game integration и полной приёмки этапа 05.

## Подэтап 03.5 — граф ресурсов и детерминированный package

Добавлены явные зависимости AssetId и пакетирование достижимых образов с target tags, SHA-256 cache и атомарной публикацией. Сцены/prefab не сканируются, платформенные форматы не создаются.

## Подэтап 04.3 — камеры и следование

Добавлен runtime CameraRig API для camera pose, пресетов и следования с параметрами. Рендеринг, Scene camera component и визуальное представление отсутствуют.

## Runtime Interaction and Events (04.4)

Реализованы programmatic runtime behavior bindings и typed interaction events. Данные поведения остаются вне Scene file.

## Подэтап 05.1 — минимальная 2D-физика и collision events

Добавлены runtime body/collider bindings, gravity, fixed-step XY AABB collision detection, базовая positional correction и sequenced Enter/Stay/Exit events. Данные остаются вне Scene serialization; ограничения — [PHYSICS_2D.md](PHYSICS_2D.md) и [отчёт](reports/05.1.md).

## Подэтап 05.2 — Game UI

Добавлена backend-neutral GameUiModel для Main Menu, Gameplay/HUD, Pause и Settings с координацией RuntimeSession. Optional SDL3/ImGui adapter и `astro_game_ui_demo` дают видимые экраны; настройки остаются in-memory. Ограничения и команды: [GAME_UI.md](GAME_UI.md), [отчёт](reports/05.2.md).

## Подэтап 05.3 — звук, анимации и игровая логика головоломки

Добавлены backend-neutral правила match-three и детерминированное sampling scalar animation tracks. Optional SDL3 adapter queues synthesized UI/gameplay cues; реальный выход проверяется через dummy driver. Нет visual puzzle board, asset audio playback, смешивания дорожек или editor authoring. Описание и ограничения: [PUZZLE_AND_ANIMATION.md](PUZZLE_AND_ANIMATION.md), [отчёт](reports/05.3.md).

## Подэтап 05.4 — Save System v1

Добавлен `astro_runtime_save`: ручные numbered slots, явно перечисляемые profile fields, game/profile/schema metadata, CRC32, ограничение размера, временная запись, атомарная замена, `.bak` и fallback при повреждении основного слота. В v1 доступны Name, scalar Transform и SpriteRenderer visibility; нет автоматического сохранения, schema migrations, tag/group rules, checkpoint/respawn, editor UI или платформенного выбора user-data root. См. [SAVE_SYSTEM.md](SAVE_SYSTEM.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт](reports/05.4.md).

## Подэтап 05.5 — Checkpoint v1

Добавлен runtime-only `CheckpointSystem` с XY area entry, условиями C++ callback, typed Activated/Rejected/Respawned events, one-shot/repeatable поведением и явным respawn. `RespawnOnly` не пишет сохранения; `SaveSelectedProfile` требует зарегистрированный profile и слот из `SaveStore`. Restore выбранного профиля отключён по умолчанию и после load выставляет respawn position. Нет scene/editor authoring, world-space/physics triggers, death detection, sound/visuals, no-code conditions или health/inventory rules. Подробности: [SAVE_SYSTEM.md](SAVE_SYSTEM.md), [отчёт](reports/05.5.md).

## Подэтап 05.6 — Windows runtime package

Optional CPack target creates a ZIP of the current SDL3/ImGui Game UI demo with third-party license texts. 05.10 adds the small authored sample campaign under `game-data/puzzle-campaign` and a `%~dp0` relative launcher. The smoke extracts the archive to a fresh unique directory, validates package files, launches the menu and bundled campaign save/restore. Arbitrary project files, Editor and build tools remain outside the ZIP; full game export is still not implemented. Original results: [report 05.6](reports/05.6.md); 05.9 refresh: [report](reports/05.6-package-refresh-after-05.9.md); self-contained sample package: [report 05.10](reports/05.10-self-contained-campaign-package.md); [BUILD_GUIDE](BUILD_GUIDE.md#упаковка-windows-runtime-этап-056).

## Подэтап 05.7 — Puzzle board in runtime Game UI

The optional SDL demo draws an 8×8 seeded Puzzle board via an additive host content callback on the Gameplay screen. Neighbor selection/swap updates the existing score/level HUD; Play resets the board. A deterministic smoke checks a valid move, rendered Gameplay draw data, HUD update, Pause/Resume/menu and isolation of the authoring Scene. It does not automate physical clicks, provide a board screenshot golden, connect campaign/save/checkpoint progress, or verify a rebuilt 05.6 ZIP. Full Stage 05 acceptance remains incomplete. Details and exact checks: [report 05.7](reports/05.7-puzzle-game-ui.md).

## Подэтап 05.8 — Authored Puzzle campaigns in standalone runtime

The SDL Game UI demo accepts a UTF-8 project root and campaign ID, validates project campaign/level files, displays the loaded authored board, and applies tile input on RuntimeSession fixed steps. After the level's target score, an explicit Continue action resolves one transition and loads the next level. `END` retains the final board/score. Added a small read-only two-level project at [samples/ПазлКампания](../samples/ПазлКампания/README.md) and a CTest smoke that runs accepted moves through both levels, follows a transition to END, and checks the result remains available. Does not persist progress or rebuild/package the updated executable. Details: [report 05.8](reports/05.8-runtime-puzzle-campaign.md).

## Подэтап 05.9 — Manual campaign progress persistence

Standalone SDL Game UI provides ten explicit SaveStore slots. The profile selects campaign ID, active level ID, terminal END flag and deterministic Puzzle snapshot; no Scene properties or project files are selected. Project-specific storage is beneath SDL user data. Restore rejects incompatible profile/game versions, campaign/level mismatch and changed authored Puzzle rules before publishing. Smoke and integration tests cover a pending completion and terminal END round-trip. No autosave, migrations, Editor Game View slots or rebuilt package are included. Details: [report 05.9](reports/05.9-campaign-progress-save.md).

## Подэтап 05.10 — Self-contained campaign in Windows runtime ZIP

The optional Windows package now includes the checked-in two-level Puzzle campaign and a command launcher that resolves both executable and campaign data relative to its own location. Fresh-extraction CTest verifies files, menu launch and campaign manual save/restore; a separate manual check verifies the `.cmd` launcher from a different current working directory. This remains an example runtime package, not arbitrary project export. No Editor, build tools, autosave, save migration, or completed Stage 05 gameplay acceptance is implied. See [report 05.10](reports/05.10-self-contained-campaign-package.md).

## 2D Editor preview GPU path

Scene/Game View рендерят SpriteRenderer quads напрямую в bgfx framebuffer. В 06.11 добавлена пиксельная smoke-регрессия нескольких текстур/alpha/resize. В 06.12 устранён квадратичный поиск Transform; оптимизированный 10k Debug CPU p50 — 24–26 ms/вид, Release — 4.7–4.9 ms/вид на D3D11. В Release собраны локальные bgfx библиотеки и полный Editor. Подробности: [отчёт 06.12](reports/06.12-dense-scene-geometry.md), [отчёт 06.11](reports/06.11-bgfx-visual-dense-release.md), решение [D046](DECISIONS.md#d046--linear-scene-transform-composition-for-dense-2d-scenes).
## 06.19 — Puzzle progress slots scoped to authored level (implemented)

The optional Puzzle save adapter now explicitly selects `puzzle.level.id` for authored-level snapshots; load rejects a mismatched ID before publishing candidate state. Editor slots use a separate `PuzzleLevels/<ID>` directory under that project's user-data namespace. The built-in Puzzle sample keeps the existing profile/path. Manual slots only; no campaign model, automatic save policy, or schema migration.
## 06.20 — Authored Puzzle level catalog (implemented)

Add bounded, safe catalog enumeration with diagnostics for malformed entries, selection/open, create-only duplication, and confirmed ID rename. Destination collisions never replace existing levels. Rename retains the existing file format, rechecks the source, publishes the new ID before removing the old path, and does not move player progress slots. No delete UI, campaign graph, or draft undo is included; see the [06.20 report](reports/06.20-puzzle-level-catalog.md).

## 06.21 — Puzzle campaign order and transition format (design)

Defined a versioned UTF-8 project document with a distinct level display order, explicit starting ID, ordered score-threshold branches, and a default level/END result. Contract: [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md); design report: [06.21](reports/06.21-puzzle-campaign-format-design.md).

## 06.22 — Strict Puzzle campaign parser and integration tests (implemented)

Implemented the bounded v1 parser, serializer, structural/reference validator, and atomic project-file API. Integration tests reject malformed syntax and invalid references and verify UTF-8 round-trip. Editor authoring, runtime transition resolution, schema migration, and rename protection remain future sub-stages; see [06.22 report](reports/06.22-puzzle-campaign-parser.md).

## 06.23 — Puzzle campaign runtime transition resolver (implemented)

Added a pure one-edge resolver: first matching descending score threshold, otherwise DEFAULT, with END represented by an empty optional. It rejects invalid campaign data and unknown completed-level IDs and never loads or recursively advances a destination. Integration coverage and limits: [06.23 report](reports/06.23-puzzle-campaign-resolver.md).

## 06.24 — Puzzle campaign runtime level loading and END state (implemented)

Added `PuzzleCampaignSession` to load START and subsequent authored levels as playable `PuzzleGame` instances. Level completion is gated on the authored Puzzle target score; the final score selects one resolver edge. Failed next-level load leaves the current completed game intact for retry. END preserves the final level/score in an `Ended` session and rejects more gameplay; creating a new session restarts from START. This does not load engine Scenes or save campaign progress. Tests and exact results: [06.24 report](reports/06.24-puzzle-campaign-session.md).

## 06.25 — Connect Puzzle campaign session to Editor Game View (implemented)

Game View starts a project campaign by ID, routes tile input through the fixed-step runtime callback, exposes explicit level continuation, and displays terminal END while retaining the final board/score. Smoke covers two authored levels, runtime input, transition loading, and END. Manual campaign progress slots were added later in 06.31; standalone SDL campaign saves remain implemented in 05.9. See [06.25 report](reports/06.25-puzzle-campaign-game-view.md).

## 06.26 — Campaign browser and link diagnostics (implemented)

Added a bounded, read-only campaign catalog API and **View → Puzzle Campaign Browser**. Valid documents show START, ordered levels, transition rules, and per-level file status; malformed documents and missing/invalid level files stay visible with diagnostics. Selecting a readable campaign populates the Game View ID field. The Editor does not modify campaign files. See [06.26 report](reports/06.26-puzzle-campaign-browser.md).

## 06.27 — Campaign authoring and safe level rename (implemented)

The campaign window edits a draft's identity, order, START and score/default/END rules, then confirms and validates writes through the project-file API. Level rename refuses when any readable campaign references the old ID, or when campaign references cannot be verified; update affected campaign links before renaming. See [06.27 report](reports/06.27-puzzle-campaign-authoring-rename.md).

## 04.12 — Editor C# consent gate (implemented)

The Play button requests one explicit, session-only approval when the current Scene has enabled ScriptBehavior components. It shows their exact bindings and verified build hashes, then recaptures before accepting. In 04.13 the runtime rechecks approval immediately before each unique host load and schedules callbacks; consent remains an execution policy, not a sandbox. The Inspector can add registered bindings disabled and supports enable/remove through transactional commands. See [04.13](reports/04.13-csharp-runtime-lifecycle.md) and [04.14](reports/04.14-scriptbehavior-editor-authoring.md).

## 04.14 — ScriptBehavior authoring in Inspector (implemented)

The selected entity Inspector lists existing script components and registry diagnostics. It can add an existing registered binding disabled, toggle it only when the build and binding metadata are current, or remove it. Typed commands carry undoable transactions; add/remove restore stable ComponentIds and component order. Scene v5 already stores this data, so no file migration is needed. Editing `.cs` source in an embedded Code workspace remains separate work. See [04.14 report](reports/04.14-scriptbehavior-editor-authoring.md).

- **04.18** Завершена design-оценка metadata-only проверки CLR и изоляции C# worker. Зафиксирована рекомендация отдельного процесса с ОС sandbox; implementation, package choice и новые функции не входят в шаг. См. [отчёт 04.18](reports/04.18-csharp-metadata-isolation-assessment.md).
- **04.19** Реализован Windows x64 metadata-validator worker prototype на `System.Reflection.Metadata` из .NET 10, с отдельным AppContainer-процессом, ограниченным protocol/input/output, Job Object и проверкой AppContainer file/network deny. Module initializer не запускается; Editor и AI пока не вызывают worker. См. [отчёт 04.19](reports/04.19-csharp-metadata-validator-worker.md) и [D084](DECISIONS.md#d084--windows-c-metadata-validator-worker-prototype-0419).
- **04.20 (реализован)** подключить AppContainer metadata worker к явной Project-panel диагностике зарегистрированных типов; ограничить assembly канонической папкой проекта, не менять Play host/ABI. Проверки и ограничения: [отчёт 04.20](reports/04.20-csharp-project-metadata-diagnostics.md), [D085](DECISIONS.md#d085--project-panel-c-metadata-diagnostics-0420).
- **04.21 (реализован)** измерена пакетная metadata-проверка; worker принимает до 64 типов за сессию, Editor разбивает до 256 типов по count/UTF-8 budget и показывает число сессий. Общий отчёт ограничен 128 KiB, без изменения sandbox/staging. См. [отчёт 04.21](reports/04.21-csharp-metadata-batch-performance.md) и [D086](DECISIONS.md#d086--c-metadata-validation-batch-limits-0421).
- **04.22 (завершён с ограничениями)** Выполнен Release-профиль доступного sample и найденной локальной Unity-сборки; Unity assembly устарела для одного скрипта и оба запрошенных типа не прошли AstroEngine ABI validation. Synthetic report smoke 128 KiB прокручивается и отрисовался за 3,27 ms. Это ограниченные данные, не benchmark большой игры; UI визуально не оценивался. Shared cache и изменения sandbox/ABI не обоснованы. Для Unity-style C# нужен отдельный product design. См. [отчёт 04.22](reports/04.22-csharp-metadata-project-ux.md) и [D087](DECISIONS.md#d087--04-22-c-metadata-profile-and-large-report-ux). Следующий пункт мастер-ТЗ — 05.1, требует отдельного старта.

## 07.2cf — framebuffer readback и надёжность Editor smoke (завершён)

В Windows D3D11 smoke добавлен BGFX blit/readback Scene и Game framebuffer и сравнение с финальным окном; зафиксированы 768/768 выборок с delta 0 на каждой view. Drag-smoke подаёт тестовые события через ImGui после SDL backend polling; 5 последовательных прогонов прошли. Это не заменяет проверку физических мышиных событий или других graphics backend. Рекомендация по расширению на цветовые/альфа сценарии выполнена в 07.2cg ниже; cross-backend tolerance требует отдельной проверки.

## 07.2cg — матрица материалов и alpha для framebuffer (завершён с ограничениями)

Проверены Scene BLEND/MASK и Game View texture/MASK/mip cases на Windows D3D11. Для texture/mip и no-light эквивалентность точная; alpha path даёт редкие outlier samples: 762/768 BLEND, 758/768 Scene MASK, 767/768 Game MASK при допуске канала 8/255. Scene alpha smoke требует 98,5%, остальные проверенные view — 99,5%. Это fixture-specific acceptance, а не утверждённая переносимая политика; Vulkan/Metal/mobile не проверялись. Полный результат в [07.2cg отчёте](reports/07.2cg-framebuffer-material-alpha-matrix.md). Рекомендация оценить доступность alternate BGFX backend выполнена в assessment 07.2ch ниже.

## 07.2ch — доступность альтернативного BGFX renderer (assessment complete)

Vulkan loader/AMD device присутствуют локально, но Editor принудительно выбирает D3D11 и отключает fallback; Editor scene shaders собираются только как Windows `s_5_0`. Это состояние не позволяет провести renderer smoke без изменения Editor/backend selection и shader pipeline. Ни Vulkan parity, ни BGFX actual Vulkan initialization не заявляются. Baseline D3D11 checks — build без изменений и focused CTest 3/3. Следующий рекомендуемый этап: спроектировать изолированный renderer-selection smoke и необходимые shader variants, сохраняя текущий D3D11 default. Подробности: [07.2ch assessment](reports/07.2ch-alternate-bgfx-backend-assessment.md).

## 07.2ci — test-only BGFX Vulkan startup smoke (завершён)

Добавлены два явных Editor smoke режима: обычный renderer default (обязан оставаться D3D11) и opt-in Vulkan. Локальная сборка создаёт SPIR-V артефакты для используемых ImGui/Scene View программ, не меняя сторонний cache или зависимости. Текущий Windows host прошёл обе инициализации. Следующий рекомендуемый шаг — отдельная Vulkan Scene View fixture/readback с ограниченным визуальным сравнением; до неё Scene/Game parity и Vulkan runtime support не объявлять. [07.2ci report](reports/07.2ci-bgfx-vulkan-startup-smoke.md).
# 07.2bw — DirectionalLight3D data contract (implemented)

Scene v12, validated optional directional-light data, transactional typed commands, and backend-neutral packet resolution are implemented. There is no Inspector authoring UI or shader lighting yet. Next recommended work: assess the BGFX shader/material integration and define a visual acceptance test before implementation.

## Vulkan Scene View visual smoke (07.2cj)

Vulkan now renders the existing 3D primitive/material fixture in Editor Scene View, and its localized screenshot-derived material pixel count is compared against the D3D11 run with a bounded tolerance. BGFX depth attachment selection is capability-driven. This validates only the current Windows x64/AMD host and the Scene View material fixture; the regular Editor remains D3D11, and Vulkan Game View is not validated. See [07.2cj report](reports/07.2cj-vulkan-scene-view-smoke.md).
## Vulkan Runtime Game View readback (07.2ck — behavior found, parity unresolved)

The explicit Vulkan Play smoke reaches the Runtime BGFX camera/mesh path, but its final-window image does not meet the existing readback contract: 563/768 samples match compared with 768/768 on D3D11. Keep the new Vulkan test visible as a CTest skip with the measured mismatch, not a green parity claim. Recommended follow-up: identify and correct the Vulkan Game View-to-ImGui/swapchain color/presentation transform, then require the same 99.5% readback threshold. See [report](reports/07.2ck-vulkan-game-view-readback.md).

### 07.2cl — Vulkan Game View color-path investigation (assessment; unresolved)

Two isolated variants were tested and rejected: explicit sRGB sampling regressed both backends to 0/768, and BGFX ImGui sRGB output mode regressed Vulkan to 0/768. Both were reverted; baseline remains Vulkan 563/768 and D3D11 768/768. Next, record the selected Vulkan swapchain format and compare a known linear/sRGB chart at attachment, sampled texture, and final-window stages. Change only a proven incorrect transfer step and retain the 99.5% threshold.

### 07.2cm — Vulkan surface format and known-color chart (diagnostic)

The current Windows AMD/Vulkan surface uses BGRA8 UNORM with sRGB-nonlinear color space. A known-color mutable BGRA8 texture sampled through ImGui matches its final-window screenshot at 4/4 samples (maximum delta 3/255). Vulkan Game View remains at 563/768 and below 99.5%; its test remains an explicit skip. The generic ImGui texture-to-window path is therefore not sufficient to explain the mismatch. Recommended follow-up: measure Game View source target pixels and UV sampling independently. See [report](reports/07.2cm-vulkan-surface-format-and-color-chart.md) and [D225](DECISIONS.md#d225--vulkan-surface-format-and-known-color-chart-072cm).

### 07.2cn — Vulkan Game View target/window tap diagnostics (complete; cause unresolved)

The opt-in smoke reports target (624×416), BGFX backbuffer (1100×720), screenshot (1280×800), viewport origin/size, BGFX UV orientation, row match counts, and five target/window pixel pairs. Vulkan remains at 563/768 (alternate orientation 564/768); the unchanged 99.5% contract fails, so the parity test stays skipped. Full Release CTest: 115 passed, 2 skipped, 0 failed of 117. Next recommended diagnostic: independently validate screenshot coordinate scaling before changing renderer sampling. See [report](reports/07.2cn-game-view-target-window-taps.md).

### 07.2co — controlled Vulkan screenshot coordinate scaling (complete; cause narrowed)

Two fixed-size runs show different results. At 1100×720, the TGA is 1280×800 and its measured Game View rectangle (726×462) differs from target (624×416), with 563/768 matches. At 1280×800, the TGA is 1280×800 and the rectangle exactly matches the 624×416 target, with 768/768 matches and maxDelta 0. This is strong evidence of a capture/window-size mapping issue on the current host, but not proof of the offending backend layer. Keep D3D11 default and Vulkan parity threshold at 99.5%. Next recommended step: measure actual Vulkan swapchain extent after resize before changing production code. See [report](reports/07.2co-vulkan-capture-coordinate-scaling.md).
Full Release CTest: **116 passed, 2 skipped, 0 failed of 118**.

### 07.2cp — Vulkan surface and screenshot extent after resize (complete; screenshot path unresolved)

For requested 1100×720, SDL drawable, BGFX reset/stats and Vulkan's `currentExtent` on the same HWND agree at 1100×720, but BGFX's screenshot TGA is 1280×800 and Game View readback remains 563/768. At 1280×800, the dimensions agree and comparison is 768/768. The mismatch is no longer attributable to the SDL drawable or the Vulkan surface current extent alone; next trace BGFX's internal framebuffer selected by the screenshot request. No production fix is included. See [report](reports/07.2cp-vulkan-swapchain-extent-after-resize.md).
Full Release CTest: **116 passed, 2 skipped, 0 failed of 118 in 189.04 s**.

### 07.2cq — BGFX invalid-handle screenshot target (complete; resize cause unresolved)

Confirmed from BGFX source that the invalid framebuffer handle selects the main `m_backBuffer`, and Vulkan screenshot readback uses its `SwapChainVK::m_sci.imageExtent`. The Game View target is not selected by this screenshot request. The 1100×720 capture still yields 1280×800; the 1280×800 case agrees. Next: isolate the sequence that updates the main backbuffer extent across resize, with a small deterministic reproduction before changing renderer behavior. See [07.2cq report](reports/07.2cq-bgfx-invalid-handle-screenshot-target.md).
### 07.2cr — BGFX Vulkan shrink-transition trace (complete; cause not yet isolated)

The diagnostic now records BGFX stats and actual TGA extents around the deterministic 1100×720 → 1280×800 → 1100×720 resize sequence. Baseline TGA is 1100×720; four post-grow TGA files are 1280×800; four post-shrink files are still 1280×800 although stats return to 1100×720 one frame after reset. Full Release CTest: **116 passed, 2 skipped, 0 failed of 118 in 207.55 s**. Next: trace Vulkan `SwapChainVK::update` and swapchain recreation at the shrink boundary before changing production code. See [07.2cr report](reports/07.2cr-bgfx-resize-frame-capture-trace.md).
### 07.2cs — Vulkan shrink swapchain recreation diagnosis (complete; no fix applied)

Temporary backend logging proved that resize-up can leave BGFX descriptor 1100×720 paired with Vulkan image extent 1280×800 via presentation scaling. Resize-down to 1100×720 then does not satisfy BGFX's recreation predicate because descriptor and recreate flag match existing state; no swapchain recreation occurs and screenshots stay at 1280×800. Backend source was restored after diagnosis. Next: prepare a narrow fix proposal and focused tests for current-surface/image-extent mismatch, grow/shrink, and stable size, then implement only under the next approved substage. See [07.2cs report](reports/07.2cs-bgfx-vulkan-shrink-recreate-root-cause.md).
### 07.2ct — BGFX Vulkan fixed-surface resize fix (complete on current host)

A version-pinned BGFX patch now recreates the Vulkan swapchain when its fixed surface `currentExtent` differs from the selected image extent, even if the BGFX descriptor is unchanged. The 1100×720 stable capture, four 1280×800 grow captures, and four 1100×720 shrink captures matched expected dimensions. Game View readback reached 768/768 samples, maxDelta 0, satisfying the existing 99.5% threshold on this Windows AMD host. Focused CTest: **3 passed**; full Release CTest: **117 passed, 0 failed, 1 skipped of 118** (mixed-DPI hardware unavailable). The patch helper targets BGFX commit `eebf5686a8c5f1c77e1af2dad17b5746ba17da8f`; other Vulkan drivers/revisions remain unverified. See [07.2ct report](reports/07.2ct-bgfx-vulkan-resize-fix.md).

### 07.2cu — BGFX patch clean-copy build verification (complete; second host unavailable)

Created clean local checkouts at the pinned BGFX/BX/BIMG revisions, applied the tracked patch, and built `bgfxRelease.lib` successfully. A second script run confirmed idempotence. Existing AstroEditor Vulkan texture/resize smoke passed **3/3** again. The separate library was not linked into AstroEditor, and no second Vulkan host was available; cross-driver behavior remains unverified. Generated MSBuild projects were reused from the same pinned BGFX staging because `genie` is unavailable locally. See [07.2cu report](reports/07.2cu-bgfx-patch-clean-build.md).

### 07.2cv — Vulkan resize repeatability (complete on one host; cross-driver check pending)

A second physical Vulkan host was unavailable. Three existing Vulkan texture, grow/shrink, and fixed-size smokes were each repeated five times on the sole detected AMD Radeon Vulkan device: **15/15 executions passed, 0 failures, 0 skips**. Verbose shrink tap showed all expected capture dimensions and **768/768** readback matches. This demonstrates repeatability only on this Windows driver. Next: run the same suite on a second Vulkan host/driver; keep Vulkan opt-in until then. See [report](reports/07.2cv-vulkan-resize-repeatability.md).

### 07.2cw — two-cycle Vulkan resize regression smoke (complete on current host)

The opt-in tap smoke now performs two resize-up/down cycles in one process, before the existing final screenshot/readback point. The test checks the ordered 1100×720 → 1280×800 → 1100×720 → 1280×800 → 1100×720 transitions and all nine BGFX capture extents. Stress CTest passed **5/5** consecutive runs; the full Release suite passed **118 tests with 1 mixed-DPI skip, 0 failures of 119**. Readback remained 768/768, maxDelta 0. This is one Windows AMD Vulkan host only; next portability evidence still needs a second Vulkan driver. See [report](reports/07.2cw-vulkan-multi-cycle-resize-smoke.md).

### 09.3d — AI-reviewed primitive creation (complete)

The existing AI Change Review panel now proposes and directly confirms a built-in primitive through `entity.create_primitive`. Preview is read-only and confirmation checks project/scene identity plus the predicted Entity ID and typed payload. Undo/Redo, serialization and asset ownership remain with existing engine paths. This is not general AI command approval or a security sandbox. Windows x64 Release: full CTest **123 passed, 0 failed, 1 skipped of 124**; mixed-DPI was unavailable. Details: [09.3d report](reports/09.3d-primitive-creation-review.md).

### 09.3e — AI-reviewed Camera3D addition (complete)

The AI Change Review panel now previews and confirms adding an inactive perspective Camera3D to the selected entity through the registered transactional `camera3d.add` command. Preview is read-only; confirmation rechecks project/scene identity and target validity. Unit coverage checks stale state, cancellation, command execution and undo/redo; an Editor window smoke saves and reloads the scene to verify that the added camera remains inactive and serialized. This remains a specific in-process confirmation flow, not broad command approval or a security boundary. Windows x64 Release full CTest: **124 passed, 0 failed, 1 skipped of 125 in 507.13 s**; mixed-DPI hardware unavailable. Details: [09.3e report](reports/09.3e-camera-addition-review.md).

### 09.3f — AI-reviewed Camera3D lens configuration (complete)

AI Change Review previews and confirms FOV, near-plane and far-plane changes through the existing transactional `camera3d.set_lens` command. The preview snapshots project/scene identity, Camera3D Component ID, old/new lens values, and active state; stale previews are rejected. No-op changes cannot be confirmed. Active state is preserved, and the UI discloses when changing the active camera will affect Game View. Unit tests cover staleness, no-op, cancellation, and Undo/Redo; an Editor window smoke persists and reloads an active camera with updated lens values. Windows x64 Release full CTest: **125 passed, 0 failed, 1 skipped of 126 in 134.27 s**; mixed-DPI hardware unavailable. Details: [09.3f report](reports/09.3f-camera-lens-review.md).

### 09.3g — AI-reviewed Camera3D active selection (complete)

AI Change Review now distinguishes activating the selected Camera3D from explicitly clearing the active selection. Its read-only preview shows the old and target camera identities, and confirmation rechecks the active-before entity/component plus target component identity before executing the existing transactional `camera3d.set_active` command. No-op and stale previews are rejected; Undo/Redo and scene persistence remain on the existing command path. The UI explains that clearing the edit-scene camera hides 3D meshes in Edit Mode Game View while Play Mode keeps its runtime scene. Windows x64 Release focused set: **11/11 passed**; activation and clear window smokes passed **3/3 each**. Full CTest: **127 passed, 0 failed, 1 skipped of 128 in 138.16 s**; mixed-DPI hardware unavailable. See [09.3g report](reports/09.3g-camera-selection-review.md).

### 09.3h–09.3j — local Transform change review (complete)

`entity.set_transform` from 09.3i is now wired into AI Change Review. The panel edits all nine local position/rotation/scale numbers, displays their before/after diff, and, when the entity has Camera3D, computes world position/target/up from a copy of the Scene. Confirmation rechecks Scene/project identity, Transform Component ID/value and the entire entity-to-root parent chain before applying the existing transaction. Unit tests cover stale ancestors, no-op, cancellation and Undo/Redo; the Editor smoke checks read-only preview, confirmation and scene save/reload. Windows x64 Release build succeeded. Focused regression: **14/14**; the new smoke passed **3/3** repeated runs; full CTest **128 passed, 0 failed, 1 skipped of 129 in 139.47 s**. Mixed-DPI remains skipped because this host lacks two displays with distinct scaling. The flow is in-process UI, not a process/security boundary. See [D258](DECISIONS.md#d258--ai-review-for-local-transform-093j) and [report](reports/09.3j-transform-review.md).

### 09.3k — stale-safe parent change review (complete)

Added transactional `entity.reparent` v1 and a focused AI Change Review control. It preserves local Transform and the descendant hierarchy; world pose is derived from the destination parent. The preview displays affected subtree size and, for Camera3D, world pose before/after. Confirmation validates project/scene identity and rechecks the source chain, destination chain, target Transform identity/value and every entity in the affected subtree before executing the registered command. Release build succeeded; focused regression **13/13**, new window smoke **3/3 repeated runs**, full Release CTest **129 passed, 0 failed, 1 skipped of 130 in 145.33 s**. Mixed-DPI remains unavailable on this host. Preserving world pose is outside this contract. This remains trusted in-process Editor UX, not a security boundary. See [D259](DECISIONS.md#d259--transactional-hierarchy-reparent-and-ai-review-093k) and [report](reports/09.3k-entity-reparent-review.md).
### 07.2cx — bounded point lighting in Scene/Game View (implemented prototype)

Added optional `PointLight3D` Scene data, v13 serialization with v1–v12 read compatibility, typed transactional commands, hierarchical world-position resolution, and a backend-neutral packet capped at four stable-EntityId lights with overflow diagnostics. Project-owned BGFX Lambert color/textured shaders consume the point-light snapshot in Scene View and Runtime Game View. The bounded attenuation is inverse-square with quadratic finite-range fade; no-light behavior and directional-light paths remain covered by regression tests. Windows x64 Release build succeeded; focused scene/lighting CTest passed **7/7**, and full Release CTest passed **119, skipped 1, failed 0 of 120**. The only skip was mixed-DPI due to unavailable distinct display scales. Inspector authoring, PBR, shadows, spots, other GPU drivers and platforms are deferred. See [D236](DECISIONS.md#d236--bounded-point-light-contract-and-rendering-072cx) and [report](reports/07.2cx-point-light-scene-game-view.md).

### 07.2cy — PointLight3D Inspector authoring (implemented)

The selected entity Inspector now adds/removes `PointLight3D` and edits enabled state, linear RGB, intensity, and range. Changes execute `light3d.add_point`, `light3d.set_point`, and `light3d.remove_point` transactions, so they preserve validation and Undo/Redo semantics. Russian labels are included. UI smoke checks the component controls, while Scene command tests cover edit/remove Undo/Redo; the point-light render smoke continues to check visible Scene/Game output. The Inspector does not add shadow, spot, or PBR controls. See [D237](DECISIONS.md#d237--pointlight3d-inspector-authoring-072cy) and [report](reports/07.2cy-point-light-inspector.md).

### 07.2cz — DirectionalLight3D Inspector authoring (implemented)

The selected-entity Inspector now adds/removes `DirectionalLight3D`, edits enabled state, linear RGB, and intensity, and displays the resolved world direction. Direction remains derived from local `-Z` through hierarchical Transform rotations; users edit it with the existing Transform controls. All mutations use typed transactional commands and retain Undo/Redo. Russian strings and smoke coverage protect the controls, command history, and existing front/back + Scene/Game lighting tests. No Scene schema or renderer change. See [D238](DECISIONS.md#d238--directionallight3d-inspector-authoring-072cz) and [report](reports/07.2cz-directional-light-inspector.md).

### 09.3l — Hierarchy drag/drop (implemented)

One entity can be dragged onto another in Hierarchy to reparent, or dropped on the Scene-root target to unparent. Both routes execute the registered `entity.reparent` transaction, keeping local Transform and enabling Undo/Redo; the standard scene save/reload path persists the relationship. The Editor smoke covers both drops, transaction history, stable Transform identity, descendants, and persistence. Full Release CTest passed **130 tests, 0 failed, 1 skipped of 131**; the skipped mixed-DPI case needs two differently scaled displays. Multi-select and world-pose-preserving reparent remain outside scope. See [D260](DECISIONS.md#d260--hierarchy-drag-drop-uses-the-reparent-transaction-093l) and [report](reports/09.3l-hierarchy-drag-reparent.md).


### 09.3m — оценка группового переподчинения (assessment complete; implementation not started)

Рассмотрены одиночная модель выбора, `entity.reparent` v1, 64-битные EntityId и возможности CommandTransaction. Рекомендация: Ctrl+Click для набора, нормализация выбранных предок/потомок до верхних корней, запрет цели внутри перемещаемого поддерева и один atomic transaction для Undo/Redo всей группы. До появления безопасной схемы массива не кодировать EntityId через `NumberArray`. Source/API не менялись, тесты проверки контрактов прошли **7/7**; полная сборка была актуальна, CMake сообщил `ninja: no work to do`. Это только оценка, а не разрешение автоматически начинать реализацию. См. [D261](DECISIONS.md#d261--оценка-контракта-группового-переподчинения-093m) и [отчёт](reports/09.3m-multi-reparent-assessment.md).
### 09.3n — групповое переподчинение Hierarchy (выполнено)

Ctrl+Click переключает сущность в наборе; drag выбранной строки переносит все выбранные верхние корни одной командой и одной записью Undo. Существующий parent/child выбор нормализуется; циклы и неизвестные сущности отклоняются. Локальные Transform сохраняются. Smoke проверяет UI-путь, Undo/Redo и round-trip сцены. Мировая поза при смене родителя не сохраняется; sibling-порядок и массовый Inspector не вводятся. Результаты: [09.3n](reports/09.3n-group-reparent.md).

### 09.3o — оценка массового Inspector (assessment завершён)

Оценка рекомендовала начать только с общего local Transform и отдельного структурированного typed payload. Исторический контракт и риски: [09.3o](reports/09.3o-multi-inspector-assessment.md), D263.

### 09.3p — групповой Inspector для local Transform (выполнено)

Общее выделение редактирует девять локальных полей Transform с mixed-value индикацией. Один изменённый field применяется к каждому выбранному объекту с сохранением его прочих локальных значений, одной записью Undo/Redo и штатной сериализацией сцены. Команда `entity.set_transforms` v1 объявляет типизированный `TransformAssignmentArray`; внешний JSON/remote codec отсутствует. Компонентные Inspector остаются primary-only. Функция, ограничения, повторяемый smoke и Release regression описаны в [отчёте 09.3p](reports/09.3p-multi-transform-inspector.md) и D264.

### 09.3q — клавиатурный ввод и устаревший draft группового Inspector (выполнено)

Smoke тест вводит числовое значение в групповой Transform Inspector через SDL/ImGui, фиксирует draft Enter без изменения Scene и проверяет блокировку Apply после внешнего изменения Transform до Reload. Полный Windows x64 Release CTest: 132 passed, 0 failed, 1 skipped из 133; пропуск требует двух дисплеев с разным DPI. Автоматическая проверка не заменяет ручную проверку физической клавиатурой. Результаты в [отчёте 09.3q](reports/09.3q-multi-transform-keyboard-stale-draft.md) и D265.

### 09.3r — сброс группового Transform draft при смене выделения (выполнено)

Editor smoke создаёт неприменённый числовой draft, меняет выделение с двух сущностей на одну, проверяет очистку draft и отображение предупреждения, затем возвращает исходную группу и повторяет Apply/Undo/Redo/save-reload. Полный Release CTest: 132 passed, 0 failed, 1 skipped из 133 за 146.54 s; пропуск — двухмониторная проверка mixed DPI. Подробности в [отчёте 09.3r](reports/09.3r-multi-transform-selection-draft-discard.md) и D266.
