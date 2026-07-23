# AleQFS — Sistema de Archivos Cuántico Linux Eficiente y Adaptativo

AleQFS es un sistema de archivos **inspirado en la computación cuántica** que simula
superposición, entrelazamiento y búsqueda de Grover — todo ejecutándose en hardware
clásico con un módulo nativo del kernel (sin FUSE).

## Características Cuánticas

- **Superposición** — Los archivos pueden existir en múltiples directorios
  simultáneamente mediante campos de amplitud de probabilidad. Leer un archivo
  "colapsa" su estado cuántico.
- **Entrelazamiento** — Los directorios pueden estar entrelazados para que las
  operaciones en uno se reflejen instantáneamente en el otro (acción fantasma a distancia).
- **Búsqueda de Grover** — Índice de búsqueda cuántico O(1) basado en hash que
  reemplaza los árboles B tradicionales. Coincidencia de patrones en tiempo constante.
- **Detección de Decoherencia** — Cada bloque lleva un checksum cuántico.
  La manipulación provoca decoherencia, haciendo detectable la corrupción de datos.
- **Temperatura Cuántica** — El sistema opera cerca del cero absoluto (0.01 K)
  para mantener la coherencia.

## Inicio Rápido

```bash
# Compilar
make

# Formatear una imagen cuántica de 64 MiB
./mkfs.aleqfs /tmp/test.qfs 64
# o: ./aleqfs format /tmp/test.qfs 64

# Verificar el estado cuántico
./aleqfs qstatus /tmp/test.qfs

# Crear directorios y archivos (con superposición)
./aleqfs mkdir /tmp/test.qfs /hello
./aleqfs create /tmp/test.qfs /hello/world.txt

# Entrelazar dos directorios
./aleqfs mkdir /tmp/test.qfs /a
./aleqfs mkdir /tmp/test.qfs /b
./aleqfs entangle /tmp/test.qfs /a /b

# Observar el estado cuántico (colapsa la superposición)
./aleqfs observe /tmp/test.qfs /hello/world.txt

# Búsqueda de Grover
./aleqfs grover /tmp/test.qfs 1

# Montar (requiere el módulo del kernel)
sudo modprobe aleqfs
sudo mount -t aleqfs /tmp/test.qfs /mnt
```

## Comandos

| Comando | Descripción |
|---------|-------------|
| `format <img> <size_mb>` | Crear y formatear una nueva imagen cuántica |
| `mkfs <device> [size_mb]` | Formatear un dispositivo o imagen |
| `ls <img> <path>` | Listar contenido del directorio (colapso con --collapse) |
| `mkdir <img> <path>` | Crear un directorio |
| `rmdir <img> <path>` | Eliminar un directorio |
| `cp-in <img> <src> <dst>` | Copiar un archivo dentro de la imagen cuántica |
| `cat <img> <path>` | Mostrar contenido del archivo |
| `stat <img> <path>` | Mostrar metadatos + estado cuántico |
| `mv <img> <src> <dst>` | Renombrar |
| `rm <img> <path>` | Eliminar un archivo |
| `create <img> <path>` | Crear un archivo vacío |
| `tree <img> <path>` | Mostrar árbol de directorios (colapso con --collapse) |
| `dump <img>` | Mostrar información del superbloque (parámetros cuánticos) |
| `entangle <img> <path_a> <path_b>` | Entrelazar dos directorios |
| `observe <img> <path>` | Colapsar el estado cuántico y leer |
| `decohere <img> <path>` | Forzar decoherencia en un archivo |
| `grover <img> <pattern>` | Búsqueda cuántica de Grover |
| `qstatus <img>` | Estado del sistema cuántico |

## Estructura del Código

```
src/
├── aleqfs.h        — Estructuras cuánticas principales y API
├── aleqfs_layout.h — Diseño cuántico en disco compartido (kernel + espacio de usuario)
├── main.c          — Punto de entrada CLI y comandos cuánticos
├── super.c         — Operaciones del superbloque cuántico
├── io.c            — Lectura/escritura del dispositivo de bloques
├── bitmap.c        — Mapa de bits de bloques/inodos libres
├── inode.c         — Asignación cuántica de inodos
├── extent.c        — E/S de archivos basada en extentos
├── dir.c           — Operaciones cuánticas de directorio (superposición)
├── path.c          — Resolución de rutas
├── grover.c        — Índice de búsqueda cuántica de Grover
├── entangle.c      — Operaciones de entrelazamiento cuántico
├── checksum.c      — Detección de decoherencia mediante checksums
├── quantum.c       — Gestión del estado cuántico
└── aleqfs_ko.c     — Módulo del kernel de Linux (controlador VFS)
```

## Opciones de Compilación

```bash
make              # Compilación de lanzamiento
make CFLAGS="-g"  # Compilación de depuración
make check        # Ejecutar suite de pruebas cuánticas
make kmod         # Compilar módulo del kernel
make deb          # Compilar paquete .deb
make install      # Instalar en /usr/local
```

## Módulo del Kernel

```bash
sudo apt install dkms linux-headers-amd64
sudo make deb
sudo apt install ./pkg/aleqfs-2.0.0.deb
sudo modprobe aleqfs
sudo mount -t aleqfs /tmp/test.qfs /mnt
```

## Tipos de archivo soportados

AleQFS soporta **archivos regulares** y **directorios** además de **directorios entrelazados**
(pares vinculados). No se soportan enlaces simbólicos, nodos de dispositivo, FIFOs ni sockets.
