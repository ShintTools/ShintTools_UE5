# ShintTools – Unreal Engine 5 Plugin

## Arquitectura General

```
┌─────────────────────────────────────────────────────────┐
│                  UNREAL EDITOR                          │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │            ShintTools Plugin (C++)                │   │
│  │                                                   │   │
│  │  FShintToolsModule  ←→  SShintToolsPanel (UI)    │   │
│  │         ↓                      ↓                  │   │
│  │  FCoreProcessManager    FShintCoreClient          │   │
│  │  (FPlatformProcess)     (FHttpModule)             │   │
│  └──────────────┬──────────────────┬────────────────┘   │
└─────────────────│──────────────────│────────────────────┘
                  │ spawn            │ HTTP REST
                  ↓                 ↓
        ┌─────────────────┐  ┌──────────────────────┐
        │ python main.py  │  │  localhost:18200       │
        │   OR            │  │  Core Engine REST API  │
        │ docker run ...  │  │  GET /health           │
        └─────────────────┘  │  GET /ping             │
                             └──────────────────────┘
```

### Principios de Diseño

| Principio | Implementación |
|-----------|----------------|
| **Thin Client** | 0 lógica de negocio en el plugin |
| **Separación** | HTTP Client, Process Manager y UI son clases independientes |
| **Extensibilidad** | Nuevos endpoints = nueva función en `FShintCoreClient` |
| **Configuración externa** | `shinttools.config.json` en raíz del proyecto |

---

## Estructura de Carpetas

```
Plugins/
└── ShintTools/
    ├── ShintTools.uplugin              ← Descriptor del plugin
    └── Source/
        └── ShintTools/
            ├── ShintTools.Build.cs     ← Reglas de compilación (módulos UE5)
            ├── Public/
            │   └── ShintTools.h        ← API pública + declaración de log
            └── Private/
                ├── ShintTools.cpp      ← Registro de módulo, menús, tabs
                ├── UI/
                │   ├── SShintToolsPanel.h    ← Widget Slate (header)
                │   └── SShintToolsPanel.cpp  ← Widget Slate (implementación)
                ├── Core/
                │   ├── ShintCoreClient.h     ← Cliente HTTP (header)
                │   └── ShintCoreClient.cpp   ← Cliente HTTP (implementación)
                └── Utils/
                    ├── CoreProcessManager.h  ← Launcher de proceso (header)
                    └── CoreProcessManager.cpp← Launcher de proceso (implementación)

shinttools.config.json                  ← Config en raíz del proyecto UE
```

---

## Archivos y Responsabilidades

### `ShintTools.uplugin`
Descriptor JSON que Unreal Engine usa para descubrir el plugin.
- Tipo de módulo: `Editor`
- Fase de carga: `PostEngineInit` (seguro para acceder al LevelEditor)

### `ShintTools.Build.cs`
Define las dependencias del módulo:
- `HTTP` + `Json` + `JsonUtilities` → cliente REST
- `LevelEditor` + `ToolMenus` → integración con menús del editor
- `Slate` + `SlateCore` → widgets UI

### `ShintTools.h` / `ShintTools.cpp`
**FShintToolsModule** – Ciclo de vida del plugin:
- `StartupModule()`: registra tab spawner y extiende el menú `Window`
- `ShutdownModule()`: limpieza completa
- Entrada de menú: `Window → ShintTools`

### `SShintToolsPanel.h` / `.cpp`
**SShintToolsPanel** – UI principal (Slate widget):
- Indicador de estado con dot coloreado (● verde/rojo/amarillo/gris)
- Tres botones de acción
- Área de log multilinea con auto-scroll
- Delegados HTTP enlazados para actualización reactiva de la UI

### `ShintCoreClient.h` / `.cpp`
**FShintCoreClient** – Cliente HTTP:
- `LoadConfig()`: carga `shinttools.config.json` con `FJsonSerializer`
- `CheckHealth()`: `GET /health`
- `Ping()`: `GET /ping`
- `SendRequest()`: método genérico con timeout de 5s
- Respuesta entregada en Game Thread via `FOnShintRequestComplete`

### `CoreProcessManager.h` / `.cpp`
**FCoreProcessManager** – Gestión de procesos:
- `StartCoreEngine()`: lanza `python main.py` o `docker run`
- `StopCoreEngine()`: termina el proceso con `FPlatformProcess::TerminateProc`
- `IsCoreRunning()`: consulta estado con `FPlatformProcess::IsProcRunning`
- `ResolveCoreScriptPath()`: busca `main.py` en plugin dir y project dir

---

## Instalación en Unreal Engine 5

### Paso 1: Copiar el plugin

```bash
# Desde la raíz de tu proyecto UE5:
cp -r ShintTools/Plugins/ShintTools  <TuProyecto>/Plugins/ShintTools
```

### Paso 2: Copiar el config

```bash
cp ShintTools/shinttools.config.json  <TuProyecto>/shinttools.config.json
```

### Paso 3: Regenerar el proyecto

**Windows:**
```
Haz clic derecho en <TuProyecto>.uproject → "Generate Visual Studio project files"
```

**Linux/Mac:**
```bash
UnrealBuildTool -projectfiles -project="<ruta>/<TuProyecto>.uproject" -game -rocket -progress
```

### Paso 4: Compilar

Abre la solución en Visual Studio / Rider y compila en modo **Development Editor**.

O desde la terminal:
```bash
# Windows
Engine/Build/BatchFiles/Build.bat <TuProyecto>Editor Win64 Development "<ruta>.uproject" -WaitMutex -FromMsBuild

# Linux
Engine/Build/BatchFiles/Linux/Build.sh <TuProyecto>Editor Linux Development "<ruta>.uproject"
```

### Paso 5: Habilitar el plugin en el Editor

1. Abre Unreal Editor
2. Ve a **Edit → Plugins**
3. Busca **"ShintTools"** en la categoría **Developer Tools**
4. Activa el checkbox
5. Reinicia el editor cuando se solicite

### Paso 6: Abrir el panel

```
Window → ShintTools
```

---

## Uso del Panel

| Botón | Acción | Endpoint |
|-------|--------|----------|
| **Check Core Engine** | Verifica conectividad | `GET /health` |
| **Start Core Engine** | Lanza proceso local | `python main.py` |
| **Ping API** | Round-trip de prueba | `GET /ping` |

### Indicador de Estado

| Color | Significado |
|-------|-------------|
| 🟢 Verde | Core Engine online y respondiendo |
| 🔴 Rojo | Core Engine offline o inaccesible |
| 🟡 Amarillo | Verificando (request en vuelo) |
| ⚫ Gris | Estado desconocido (no se ha verificado) |

---

## Configuración (`shinttools.config.json`)

Coloca este archivo en la raíz del proyecto UE (junto al `.uproject`):

```json
{
  "core_port": 18200,
  "auto_start_core": true
}
```

| Campo | Tipo | Default | Descripción |
|-------|------|---------|-------------|
| `core_port` | `int` | `18200` | Puerto TCP del Core Engine |
| `auto_start_core` | `bool` | `true` | Si el plugin debe iniciar el Core automáticamente |

---

## Logging

Todos los logs del plugin usan la categoría **`LogShintTools`**:

```
LogShintTools: Log    → información normal
LogShintTools: Warning → advertencias (Core offline, fallo de proceso)
LogShintTools: Error   → errores críticos
```

Ver en: **Window → Developer Tools → Output Log** → filtrar por `LogShintTools`

---

## Extender el Plugin

### Añadir un nuevo endpoint

En `ShintCoreClient.h`:
```cpp
void MyNewAction(FOnShintRequestComplete OnComplete);
```

En `ShintCoreClient.cpp`:
```cpp
void FShintCoreClient::MyNewAction(FOnShintRequestComplete OnComplete)
{
    SendRequest(TEXT("/my-endpoint"), EShintHttpMethod::POST, TEXT("{}"), OnComplete);
}
```

En `SShintToolsPanel.cpp`: añade un botón y su handler que llame a `CoreClient->MyNewAction(...)`.

---

## Requisitos

| Requisito | Versión mínima |
|-----------|---------------|
| Unreal Engine | 5.3+ |
| Visual Studio | 2022 (Windows) |
| Clang | 15+ (Linux/Mac) |
| Core Engine | Python 3.8+ o Docker |
