# Editor — CLAUDE.md

Wskazówki dla pracy w `Editor/`. Uzupełnia główny `CLAUDE.md` z roota repo.

## Zabrudzanie assetów (dirty marking) w viewportach i panelach

**Reguła:** każda mutacja danych assetu zrobiona przez viewport albo panel **musi** oznaczyć asset jako brudny. Dzięki temu edytor pokazuje stan „niezapisane zmiany" (ikona `UnsavedDocument` na oknie, popup „Assets are unsaved" przy zamknięciu), a Ctrl+S / „Save All" wie co zapisać.

Dwie metody do tego:
- `IEditorViewport::ViewportChangedAsset()` — woła z poziomu viewportu; brudzi asset viewportu (`mAsset`).
- `IEditorPanel::PanelChangedAsset()` — woła z poziomu panelu; deleguje do `ViewportChangedAsset()` rodzica.

Obie sprowadzają się do `EngineAssetManager::MarkAssetDirty(...)`.

### Jak to stosować

- Większość kontrolek refleksji zwraca `bool` informujący o zmianie — owijaj w `if`:
  ```cpp
  if (TypeSerializer<Vec3>::EditorControl(&data, "Kolor")) {
      PanelChangedAsset();
  }
  ```
  Tak samo surowe ImGui: `if (ImGui::ColorEdit3(...)) PanelChangedAsset();`, `if (ImGui::Checkbox(...)) ...`.
- Akcje bez wartości zwrotnej (przyciski, spawn/usuń obiekt, dodaj/usuń sub-shape) — wołaj zabrudzacz zaraz po wykonaniu mutacji.
- Mutacje przez zdarzenia (np. spawn/destroy GameObjectów w scenie) lepiej łapać raz, subskrybując event świata, niż rozsiewać wołania po wielu miejscach. Patrz `SceneViewport::SubscribeToCurrentWorld()` — subskrypcja `"GameObjectsChanged"` → `ViewportChangedAsset()`.

  Watch the subscription window when you do this: loading and unloading a scene spawns and destroys
  every object in it, and both dispatch `"GameObjectsChanged"` — neither is a user edit. `SceneViewport`
  therefore drops the subscription before `ConnectToWorld()` and takes it again after (`OnInit`), keyed
  on the world it actually listens to (`mSubscribedWorld`). Without that, opening a scene while another
  one is open marked the freshly opened asset dirty (`mAsset` is already the new scene when the old
  world's destroy batch fires), and the reused viewport kept listening to the destroyed world, so real
  edits stopped dirtying anything.

### Czego NIE brudzić

- View-only state that is not part of the asset's data — e.g. `StaticMeshViewport::Material` used only for preview, the physics debug visualization mode/colors (`PhysicsWorld::DebugRenderMode`, `DebugLineColor`, `DebugPointColor`), the viewport camera. These are view settings, not the asset.
- Rearrangements the editor does to itself. The python hot reload destroys and respawns the scene's
  python objects, rebuilding each one from its own serialized state — the scene file is unchanged, so
  `SceneViewport` raises `mSuppressDirtyFromWorld` around `ReloadPythonInstances` and the
  `"GameObjectsChanged"` handler skips the dirty mark. Any future "recreate from the object's own
  data" pass needs the same treatment.

## Nazwy obiektów w Structure panelu

Lista bierze `GameObject::GetObjectName()` (trwała nazwa z JSON-a sceny), **nie**
`EngineObject::GetDisplayName()`. Zmiana nazwy: podwójny klik w wiersz albo „Rename" z menu
kontekstowego → `InputText` w miejscu wiersza; Enter/klik poza polem zatwierdza, Esc anuluje.
`CommitRename` ignoruje nazwę pustą i niezmienioną, więc Esc nie brudzi assetu.

Pułapka przy duplikowaniu: serializacja obiektu wiezie w JSON-ie rzeczy, których klon **nie** ma
odziedziczyć. Wszystkie ścieżki „Duplicate" wołają dlatego `SceneStructurePrepareClone(j, world, source)`
przed `LoadGameObjectFromJSON`, a nie składają kroków po swojemu. Funkcja robi trzy rzeczy:

- **nazwa** (`PLU_PROPERTY`, więc jedzie w JSON-ie) → kolejny wolny numerek *tego samego prefiksu*
  (`SceneWorld::MakeDefaultObjectNameFromBase`), więc duplikat `Tree3` nazywa się `Tree4`, a nie
  domyślnym `StaticMeshActor7`;
- **UUID** → świeży. `LoadGameObjectFromJSON` honoruje `uuid` z JSON-a (to on trzyma tożsamość obiektu
  przy wczytywaniu sceny i hot reloadzie Pythona), więc bez podmiany klon wszedłby pod UUID oryginału;
- **attachment** → wyrzucony, klon wychodzi wolny. Samo wyrzucenie nie wystarcza: `location`/`rotation`/
  `scale` w JSON-ie są **względne** wobec rodzica, więc klon stanąłby w tym offsecie licząc od środka
  świata. Transform przepisujemy więc na światowy z oryginału.

Przy duplikowaniu N razy wołaj to w każdej iteracji — poprzedni klon zajął już swój numerek i UUID.
Dodając nową ścieżkę duplikowania przez serializację, użyj tej funkcji.

Kolejność listy: grupy po prefiksie nazwy (alfabetycznie), a w grupie **malejąco** po numerku —
`Tree5` na górze, `Tree0` i nazwy bez numerka na dole (`SceneStructureSortByName`). Sortowane są
razem tablice obiektów i nazw, bo indeks wiersza jest kotwicą zaznaczenia (Shift+klik).

Na wierzchu sortowania po nazwie siedzi **hierarchia attachmentów** (`SceneStructureOrderByAttachment`):
obiekt podpięty przez `GameObject::AttachToObject`/`AttachToComponent` ląduje zaraz pod swoim
rodzicem, o jeden poziom wcięcia głębiej (`mListDepths`), a nazwy porządkują już tylko rodzeństwo.
Funkcja **wyłącznie przestawia** tablice — każdy obiekt świata wychodzi z niej dokładnie raz (sierota
albo obiekt w cyklu ląduje na poziomie roota), bo indeks wiersza dalej jest kotwicą zaznaczenia.
Przeciągnięcie wiersza na inny podpina (`KeepWorld`), upuszczenie na pustą przestrzeń pod listą albo
„Detach" z menu kontekstowego odpina. Każda taka zmiana ustawia `mListDirty` — attachment zrobiony
z kodu/Pythona tego nie robi, więc lista pokaże go dopiero po najbliższej odbudowie.

**Lista jest cache'owana między klatkami** (`mListObjects`/`mListNames`) — budowanie jej co klatkę
to przy tysiącu obiektów ~1 ms (kopia tablicy + kopia nazw + sortowanie). Odbudowa (`mListDirty`)
leci z trzech źródeł:
- subskrypcja `"GameObjectsChanged"` na **aktualnym** świecie (`EnsureSubscribedTo` przepina ją,
  gdy `GetCurrentWorld()` się zmieni — PIE/overlay podmieniają świat pod panelem),
- `CommitRename` (zmiana nazwy z poziomu panelu),
- `SceneObjectDetailsPanel` — `mObjectName` jest `PLU_PROPERTY`, więc da się je zmienić
  w inspektorze z pominięciem `SetObjectName`; panel porównuje nazwę przed/po `EditorControl`
  i przy zmianie dispatchuje `"GameObjectsChanged"`.

**Dokładając nową ścieżkę, która zmienia zestaw obiektów albo ich nazwy poza `SpawnGameObject`/
`DeleteGameObject`, zadispatchuj `"GameObjectsChanged"` na świecie** — inaczej Structure pokaże
nieaktualną listę. Wiersze z martwym `TUsePointer` są pomijane i brudzą cache (siatka
bezpieczeństwa, nie zamiennik eventu).

## Zaznaczanie GameObjectów (primary + multi-selection)

`gEditorAppContext->EditorState.SelectedGameObject` pozostaje **pojedynczym, globalnym**
zaznaczeniem — to je czyta details panel, gizmo, Delete w `SceneViewport` i „Fit In View".
Multi-selection **nie** jest globalny: żyje lokalnie w `SceneStructurePanel::mSelectedObjects`.

Reguła synchronizacji (`SceneStructurePanel::SyncSelection`, wołana co klatkę):
- `SelectedGameObject` (primary) **zawsze** jest jednym z elementów `mSelectedObjects`.
- Jeśli primary został ustawiony **poza panelem** (klik w viewporcie) i nie ma go w tablicy →
  multi-selection kasuje się do tego jednego obiektu. Zaznaczenie z zewnątrz wygrywa, bo
  inaczej podświetlenie w Structure rozjechałoby się z tym, co edytuje details panel.
- Handle nieżywych obiektów są wyrzucane co klatkę (usunięcie Deletem, przeładowanie sceny).

Sterowanie: klik = pojedynczo, Ctrl+klik = toggle, Shift+klik = zakres od kotwicy
(`mSelectionAnchor`, indeks w liście wierszy panelu).

### Kasowanie

Delete łapie `SceneViewport::OnUpdate` (całe okno, niezależnie od tego, który panel ma focus),
ale **samo kasowanie oddaje `SceneStructurePanel::DeleteSelectedObjects()`** — multi-selection
żyje tylko w panelu, więc viewport nie ma skąd wziąć pełnej listy. Ta sama metoda siedzi pod
„Delete" w menu kontekstowym panelu. Gdy panelu nie ma (nie znalazł go `GetPanelSlow`), viewport
spada na starą ścieżkę „skasuj primary".

`DeleteGameObject` jest **odroczone** (`mObjectsToDestroy` przetwarza się na końcu klatki), więc
obiekty są jeszcze żywe po wołaniu — zaznaczenie i primary czyścimy od razu, żeby details panel
nie edytował przez tę klatkę trupa.

Gdyby multi-selection miał kiedyś działać w viewporcie/gizmo, trzeba go przenieść do
`EditorAppState` — wtedy **każde** miejsce ustawiające `SelectedGameObject` musi też
zaktualizować tablicę, inaczej wróci rozjazd, przed którym broni się `SyncSelection`.

## Kamera viewportu (jedna kamera, per-viewport stan)

W edytorze istnieje **jedna** `EditorSceneCamera` (`gEditorAppContext->EditorSceneCamera`), a każdy viewport pamięta tylko swój **widok** (lokacja, „nice" rotacja, `CameraOptions`) i dostaje go z powrotem przy wejściu — z punktu widzenia usera każdy viewport ma „swoją" kamerę.

Zakres jest celowo minimalny: **sam widok, nic o tym co się renderuje**. Viewport nie dotyka `SetEditorRenderCamera`, nie tworzy/nie kasuje overlaya poza swoim `OnOpened` i nie sprawdza, czy scena jest wczytana. Patrz „Znany problem: PIE" niżej — próby robienia tego stąd kończyły się regresjami.

- Nowy viewport nawigujący w 3D → nadpisz `IEditorViewport::UsesEditorCamera()` na `true`. Bez tego viewport nigdy nie przejmie kamery (i dobrze — viewporty typu Texture/Shader nie mają jej ruszać).
- Kamerę bierz przez `IEditorViewport::GetEditorCamera()`, **nie** twórz własnej instancji.
- Właściciela trzyma `EditorViewportManager` (`SetCameraOwner`/`GetCameraOwner`); przekazanie zapisuje stan poprzednika (`SaveCameraState`) i przywraca stan nowego (`ApplyCameraState`).
- Przejęcie dzieje się samo: na hoverze panelu (`IEditorPanel::BeginPanel`) i przy pojawieniu się viewportu (`IEditorViewport::BeginWindow`, po `OnOpened` paneli). Nie wołaj `ClaimEditorCamera()` ręcznie bez powodu.
- Pierwsze przejęcie **adoptuje** aktualny stan kamery zamiast go nadpisywać — dzięki temu pozycja wczytana ze sceny (`EditorCameraLocationLoaded`) czy startowy framing panelu stają się widokiem startowym viewportu.
- Dlatego framing (`NeedsFraming`) ustawiaj tylko raz (wartość początkowa pola) albo z przycisku „Frame" — **nie** w `OnOpened` panelu, bo skasowałby zapamiętany widok przy każdym powrocie do zakładki.

### Znany problem: PIE (nierozwiązany)

**W PIE viewporty meshy stoją** — `StaticMeshViewportPanel`/`SkeletalMeshViewportPanel` mają bramkę `if (!IsInPIE())` na sterowaniu kamerą, a „Frame" nie ma czego objąć. Nie jest to do naprawienia z poziomu viewportu i **nie próbuj tego łatać lokalnie** (było, wróciło regresjami). Przyczyny są architektoniczne:

- `SceneManager::EnterPIE()` woła `UnloadOverlayScene()`, a panel meshu odtwarza overlay tylko w `OnOpened`, czyli przy zmianie widoczności. Viewport widoczny w momencie Play zostaje bez swojego meshu do końca sesji PIE.
- Overlay to **singleton** (`mOverlayScene`), a `GetCurrentWorld()` = `overlay ?: PIE ?: active` — z czego korzysta **i tick** (`SceneManager::OnUpdate`) **i renderer** (`RenderSnapshotBuilder`). Odtworzenie overlaya w PIE zatrzymuje więc grę: nie tickuje i nie renderuje.
- Jest **jeden** główny FBO (`RequestMainFrameBuffer`) i **jeden** `RenderSnapshot` z jedną kamerą, więc PIE i podgląd meshu nie mogą renderować się równolegle.

Docelowo: wiele scen otwartych naraz + widok per viewport w snapshocie (własny FBO + własny świat + własna kamera). Uwaga na sprzężenie: **przy równoległych widokach każdy viewport potrzebuje żywej kamery co klatkę**, więc opisany wyżej model „jedna kamera + zapamiętany stan" przestaje wystarczać i zamienia się w realną kamerę per viewport (tak miały Skeleton/Animation przed ujednoliceniem). Skeleton/Animation działają w PIE, bo rysują ImGui draw listami i nie dotykają ścieżki renderu w ogóle.

## Power saving: ask for your frames

With `Power Saving` on (default) the editor only builds frames on demand: every OS event buys one second
of them, and after that the UI stands still until the next event. A panel or viewport that changes on its
own has to say so, each frame it draws (`PluEngine/FrameDemand.h`, table in `HELPERS.md`):

- animating at full rate (playing preview, simulation, progress of a running job) → `RequestContinuousRedraw()`;
- slowly changing numbers (stats, profiler) → `RequestRedrawAfter(kStatsRefreshSeconds)`;
- background work that just finished and changes what is shown → `RequestRedraw()`.

Call it only while the thing is actually visible and running (inside `BeginPanel()`, guarded by the
"is playing" flag) — an unconditional call turns power saving off for as long as the panel is open.
PIE is exempt: it always runs every frame.

A bare pointer move (no button held) only buys **probe frames**: the UI is built, shown only if its draw
data changed, and the scene is not re-rendered. So anything that changes the *scene* on plain hover (not
the ImGui draw data) must call `RequestRedraw()` itself, and a tick that queues per-frame data for the
render snapshot should expect it to be discarded on such frames (`IsProbeFrame()`).

## Zapis na żądanie (NIE zapisuj od razu)

Nie zapisuj assetu natychmiast po mutacji. Oznacz go brudnym i zostaw zapis ścieżce na żądanie (Ctrl+S w `BeginWindow`/`BeginPanel`, albo „Save All"). Wszystko idzie przez jeden punkt:

```
SaveAsset(assetDesc) → DispatchAssetSaveJSON / DispatchAssetSaveBinary
                     → mAssetLoaders[AssetType->TypeName]->DispatchAssetSave(...)
```

- Loadery (`IAssetLoader`) auto-rejestrują się po `GetSupportedAssetType()`. Nowy typ assetu z własnym zapisem → nadpisz `DispatchAssetSave` w jego loaderze (edytor-only). Przykłady: `SceneAssetHandler` (deleguje do `SceneManager::SaveActiveScene`), `StaticMeshAssetHandler` (woła `MeshImporter::SaveStaticMesh`).
- `IEditorViewport::mCanBeSaved` domyślnie = `LoaderType == JSON`. Dla assetów **binarnych** (np. StaticMesh) trzeba jawnie włączyć zapis: `SetCanBeSaved(true)` w `OnInit()`, inaczej Ctrl+S nie zadziała.

Powiązane: `EngineAssetManager` (`MarkAssetDirty`/`IsAssetDirty`/`AreAnyAssetsDirty`/`SaveAsset`), `IEditorViewport`, `IEditorPanel`.

## Model okien edytora (multi-window)

Edytor może rozrzucić panele i viewporty po kilku oknach systemowych. Okna **silnika** trzyma
`WindowsManager` (`ApplicationInfo::AppWindowsManager`, patrz `HELPERS.md` i `MULTITHREADING.md`),
a ich edytorskie odpowiedniki — `EditorWindowsManager` (`Editor/EditorWindows/`), po jednym
`EditorWindowInfo` na okno.

| Kind | Zawartość | Co hostuje |
|---|---|---|
| `Main` | toolbar + menu + kontrolki okna + root dockspace | wszystko; okno 0, zamknięcie = koniec aplikacji |
| `Dockspace` | toolbar + własny root dockspace | `EditorPanel`, `IEditorViewport` |
| `SinglePanel` | pasek tytułu z samymi kontrolkami okna + jeden panel na całość | dokładnie jeden `IEditorPanel` |

Zasady, o które łatwo się potknąć:

- **Każde okno ma własny kontekst ImGui.** `PluEditor::OnTick` buduje po jednej klatce na okno
  (`OnImGuiRenderForWindow`, rozgałęzienie po `Kind`). Wszystko, co woła ImGui, musi lecieć
  w kontekście swojego okna — `ImGui::GetMainViewport()` zwraca viewport bieżącego kontekstu, więc
  `DrawMainEngineWindow`/`DrawToolbarWindow` działają per okno bez zmian.
- **Docking działa tylko w bieżącym kontekście.** `EditorPanelManager::DockNewPanels(windowID)`
  i `EditorViewportManager::DockNewViewports(windowID)` dokują wyłącznie wpisy przypisane do tego
  okna; reszta czeka w kolejce na swoją turę w tej samej klatce. Dockspace i `ImGuiWindowClass`
  siedzą w `EditorWindowInfo` (dawniej globalne `gDockspaceId`/`gWindowClass` — znaczyły „ostatnio
  narysowane okno", co przy wielu oknach jest błędem).
- **Przypisanie do okna trzyma sam obiekt** — `EditorPanel`, `IEditorViewport` i `IEditorPanel`
  mają `Get/SetWindowIDToRender`. Pętle rysowania filtrują po nim, a rzeczy robione „raz na klatkę"
  (czyszczenie zamkniętych paneli/viewportów) zostają na oknie 0.
- **Panel viewportu dziedziczy okno viewportu w chwili rejestracji** (`mPanelsToRegister` →
  `SetWindowIDToRender(mWindowIDToRender)`). Bez tego panel zostawał z domyślnym 0, a viewport
  otwarty w oknie wtórnym nie rysował żadnych paneli — `UpdatePanels` bierze tylko te, których
  okno zgadza się z oknem viewportu.
- **Panel wyjęty do własnego okna przestaje być rysowany przez swój viewport**
  (`IEditorViewport::UpdatePanels` go pomija) i nie dostaje `ImGuiWindowClass` rodzica — ta klasa
  jest przypięta do dockspace'u żyjącego w innym kontekście. W oknie `SinglePanel` panel **jest**
  oknem, więc `BeginPanel` dokłada mu `NoTitleBar | NoResize | NoMove | NoCollapse | NoDocking |
  NoSavedSettings` — inaczej dostajesz drugi pasek tytułu z tą samą nazwą, uchwyt resize ImGui
  w środku ramki resize okna OS i podgląd dokowania do dockspace'u, którego tam nie ma.
- **Nowo otwarty panel/viewport ląduje w oknie, które ma fokus** (`TryGetActiveWindowID`), chyba
  że czeka na niego wpis z odtwarzanego layoutu. Okno `SinglePanel` nigdy nie jest celem — hostuje
  dokładnie jeden panel. Viewport już otwarty zostaje tam, gdzie jest — „otwarcie" go tylko wyciąga
  na wierzch.
- **Tytuł okna OS jest wyliczany co klatkę z jego zawartości** (`UpdateWindowContents`): jedno
  dziecko → jego tytuł, więcej → `Group of N Windows`. Nazwy ImGui wiozą id za `##`
  (`"Details##SceneViewport"`), więc lecą przez `StripImGuiIDFromName` — do paska tytułu trafia
  sama etykieta. Okno 0 jest wyjątkiem: jego tytuł należy do projektu.
- **Okno bez zawartości jest zamykane** — ale dopiero gdy wcześniej coś w nim było
  (`EditorWindowInfo::HadContent`). Okno odtworzone z layoutu jest legalnie puste do czasu, aż
  otworzysz jego panele i wczyta się projekt z jego viewportami; bez tej bramki znikałoby zaraz
  po starcie. Okno 0 nie jest zamykane nigdy.
- **Zamknięcie okna oddaje jego zawartość** — `EditorPanel`e i viewporty wracają do okna 0
  (`ReturnPanelsFromWindow` / `ReturnViewportsFromWindow`), a panele viewportów **do swojego
  viewportu**, nie do okna 0 (`ReturnViewportPanelsFromWindow`, wołane *po* tamtych, żeby trafiły
  tam, gdzie viewport właśnie wylądował). Pominięcie tego ostatniego = panel z zamkniętego okna
  `SinglePanel` znika, bo dalej celuje w id, dla którego nikt nie buduje klatek. Samo zamknięcie
  jest odroczone do początku następnej klatki — prośba przychodzi zwykle z menu rysowanego
  *wewnątrz* zamykanego okna.
- **Układ przeżywa restart i jest per projekt**: `<projekt>/Config/EditorWindowsLayout.json`
  (okna, ich geometria i zawartość) + jeden `Config/Layout/imgui_window_<slot>.ini` na okno
  (dokowanie w środku). Bez otwartego projektu nie ma czego zapisywać ani skąd wczytywać — okna
  wtórne powstają dopiero **przy otwarciu projektu**, razem z zawartością
  (`RestoreLayoutAfterProjectOpen`), żeby przy launcherze nie wisiały puste. Zapis idzie do
  projektu, dla którego layout **wczytano** (`mLayoutProjectDir`), nie do aktualnie otwartego —
  przy przełączeniu projektu nowy jest już bieżący, a ten zapis należy jeszcze do starego.
  Przełączenie projektu w trakcie sesji zapisuje stary layout, zamyka jego okna i wczytuje nowy.
  Zawartość wraca **dopiero po otwarciu projektu** (`RestoreLayoutAfterProjectOpen`): viewportów nie ma czym wcześniej rozwiązać, a
  panele, które otwiera sam `OpenProject` (asset browser), inaczej poszłyby za regułą fokusu
  zamiast do swojego zapisanego okna. Rozwiązanie: `mPendingPanelPlacements` — panel **konsumuje**
  swój wpis przy otwarciu (`TryTakePendingPanelWindow`), więc layout stawia go dokładnie raz,
  a potem obowiązuje już fokus. Zapisywany jest **slot**, nie `windowID`: id są per sesja
  i pokrywają się tylko dlatego, że okna odtwarzamy w tej samej kolejności (to samo trzyma pliki
  .ini przy właściwych oknach).
- **Layout zapisuj przy prośbie o zamknięcie, nie dopiero w `OnShutdown`.** Wyjście z edytora
  przychodzi zwykle jako close-request do **wszystkich** okien naraz (`SDL_EVENT_QUIT`, „close all
  windows" z WM), a rekordy okien wtórnych giną klatkę później — `OnShutdown` widziałby wtedy samo
  okno główne i nadpisywał nim dobry zapis. Stąd `SaveLayout` w `OnRequestedWindowClose`
  + `mLayoutSavedOnQuit`, które blokuje ten drugi zapis. Z tego samego powodu
  `WindowsManager::Shutdown` leci w `Application::Run` **po** `OnShutdown()` — inaczej okna nie
  żyją już w momencie zapisu.

### Nadal jedno FBO sceny

Multi-window nie ruszył ograniczenia opisanego wyżej („Znany problem: PIE"): jest **jeden** główny
FBO (`RequestMainFrameBuffer`) i **jeden** `RenderSnapshot` z jedną kamerą. Przeniesienie
`SceneViewport` do drugiego okna zadziała od strony UI, ale dwa widoki 3D naraz nadal nie renderują
się niezależnie — to wymaga FBO + świata + kamery per widok w snapshocie i jest osobnym zadaniem.
Okna wtórne rysują wyłącznie ImGui na wyczyszczonym backbufferze.
