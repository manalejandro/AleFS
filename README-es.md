# AleFS — Sistema de Archivos Eficiente Adaptativo para Linux

AleFS es un sistema de archivos para Linux con una herramienta de usuario y un
módulo de kernel nativo (sin FUSE). Utiliza un árbol B+ para metadatos,
almacenamiento basado en extensos e incluye un diario para recuperación tras
fallos.

## Características

- **Metadatos con Árbol B+** — Búsquedas O(log n) para directorios y archivos
- **Almacenamiento por Extensos** — Bloques contiguos para datos de archivo
- **Asignación con Bitmap** — Seguimiento eficiente del espacio libre
- **Diario (Journal)** — Soporte para recuperación tras fallos
- **Módulo de Kernel Nativo** — Integración directa con VFS, sin FUSE
- **Herramienta de Usuario** — Formatear, inspeccionar y manipular imágenes sin
  necesidad de montar

## Inicio Rápido

```bash
# Compilar
make

# Formatear una imagen de 64 MiB
./mkfs.alefs /tmp/test.img 64
# o: ./alefs mkfs /tmp/test.img 64

# Crear directorios y archivos
./alefs mkdir /tmp/test.img /hola
./alefs create /tmp/test.img /hola/mundo.txt
./alefs ls /tmp/test.img /hola

# Copiar un archivo
echo "hola desde AleFS" > /tmp/origen.txt
./alefs cp-in /tmp/test.img /tmp/origen.txt /hola/mundo.txt

# Leer su contenido
./alefs cat /tmp/test.img /hola/mundo.txt

# Montar (requiere el módulo del kernel)
sudo modprobe alefs
sudo mount -t alefs /tmp/test.img /mnt
```

## Comandos

| Comando | Descripción |
|---------|-------------|
| `format <img> <tamaño_mb>` | Crear y formatear una imagen nueva |
| `mkfs <dispositivo> [tamaño_mb]` | Formatear un dispositivo o imagen |
| `ls <img> <ruta>` | Listar contenido de un directorio |
| `mkdir <img> <ruta>` | Crear un directorio |
| `rmdir <img> <ruta>` | Eliminar un directorio |
| `cp-in <img> <origen> <destino>` | Copiar un archivo dentro de la imagen |
| `cat <img> <ruta>` | Mostrar contenido de un archivo |
| `stat <img> <ruta>` | Mostrar metadatos de un archivo |
| `mv <img> <origen> <destino>` | Renombrar un archivo o directorio |
| `rm <img> <ruta>` | Eliminar un archivo |
| `create <img> <ruta>` | Crear un archivo vacío |
| `tree <img> <ruta>` | Mostrar árbol de directorios |
| `dump <img>` | Mostrar información del superbloque |

El enlace simbólico `mkfs.alefs` puede usarse directamente:
```
mkfs.alefs [opciones] <dispositivo> [tamaño_mb]
```

## Opciones de Compilación

```bash
make              # Compilación de producción
make CFLAGS="-g"  # Compilación con símbolos de depuración
make check        # Ejecutar pruebas
make kmod         # Compilar módulo del kernel (requiere linux-headers)
make deb          # Generar paquete .deb con soporte DKMS
make install      # Instalar en /usr/local
```

## Módulo del Kernel

El módulo del kernel requiere DKMS y linux-headers en el sistema destino:

```bash
sudo apt install dkms linux-headers-amd64
sudo make deb
sudo apt install ./pkg/alefs-1.0.0.deb
sudo modprobe alefs
sudo mount -t alefs /tmp/test.img /mnt
```

## Estructura del Código

```
src/
├── alefs.h        — Estructuras principales y API
├── main.c         — Punto de entrada y comandos CLI
├── io.c           — Lectura/escritura de dispositivos
├── super.c        — Operaciones del superbloque
├── bitmap.c       — Bitmap de bloques/inodos libres
├── inode.c        — Asignación de inodos
├── extent.c       — E/S de archivos por extensos
├── btree.c        — Árbol B+ de metadatos
├── dir.c          — Operaciones de directorios
├── path.c         — Resolución de rutas
├── journal.c      — Diario de recuperación
├── checksum.c     — Sumas de verificación
├── alefs_layout.h — Formato en disco compartido (kernel + usuario)
├── alefs_ko.c     — Módulo del kernel Linux (controlador VFS)
tests/
├── test_basic.sh  — Pruebas de operaciones básicas
└── test_stress.sh — Pruebas de estrés y casos límite
dkms/
├── dkms.conf      — Configuración de compilación DKMS
└── Makefile       — Makefile del módulo del kernel
scripts/
├── postinst       — Script post-instalación Debian
├── postrm         — Script post-eliminación Debian
└── preinst        — Script pre-instalación Debian
```

## Licencia

MIT — ver [LICENSE](LICENSE)
