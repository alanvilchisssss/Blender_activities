# Blender_activities
Proyecto de computación gráfica
Inicio de actividades
Necesario para el proyecto: instalar GIT LFS
Instrucciones para inicializar el repo

Para subir modelos 3D (como `.blend`, `.fbx` o `.obj`) a GitHub, hay un factor crucial que debes considerar: **GitHub tiene un límite estricto de 100 MB por archivo**. Como los modelos 3D y sus texturas suelen superar este tamaño fácilmente, necesitas configurar **Git LFS (Large File Storage)** desde el principio para que GitHub no rechace tus archivos.

Aquí tienes el proceso paso a paso usando la terminal (la forma más segura de configurar LFS):

1. **Crea el repositorio en GitHub:** Desde tu navegador.
1. Entra a tu cuenta en GitHub.
2. En la esquina superior derecha, haz clic en el ícono de **+** y selecciona **New repository**.
3. Ponle un nombre (ej. `mis-modelos-3d`).
4. Marca la casilla **"Add a README file"** (esto facilita la clonación inicial).
5. Haz clic en **Create repository**.


2. **Instala Git y Git LFS:** Requisito indispensable para archivos pesados.
Si aún no los tienes, necesitas instalar las herramientas en tu computadora:

1. Descarga e instala [Git](https://git-scm.com/).
2. Descarga e instala [Git LFS](https://git-lfs.com/).


3. **Clona tu repositorio y activa LFS:**
Abre tu terminal (Símbolo del sistema, PowerShell o Terminal en Mac) y ejecuta los siguientes comandos:

1. Clona el repositorio a tu computadora:
`git clone https://github.com/TU_USUARIO/mis-modelos-3d.git`
2. Entra a la carpeta del repositorio:
`cd mis-modelos-3d`
3. Inicializa Git LFS en ese repositorio:
`git lfs install`


4. **Indica qué archivos 3D usarán LFS:** El paso más importante.
Debes decirle a Git qué extensiones de archivo debe tratar como archivos grandes. En tu terminal ejecuta:

`git lfs track "*.blend"`
`git lfs track "*.fbx"`
`git lfs track "*.png"` *(Si tienes texturas muy pesadas)*

Esto generará un archivo oculto llamado `.gitattributes`. Debes subir este archivo a GitHub **antes** de subir tus modelos:

`git add .gitattributes`
`git commit -m "Configurar Git LFS para modelos 3D"`
`git push origin main`


5. **Sube tus modelos de Blender:**
Ahora, simplemente guarda tu archivo `.blend` y tus exportaciones (`.fbx`, `.obj`, texturas) dentro de la carpeta `mis-modelos-3d` en tu computadora. Cuando quieras subir una actualización:

1. `git add .` (para añadir todos los cambios nuevos)
2. `git commit -m "Actualizar modelo del personaje principal v2"` (un mensaje que describa el cambio)
3. `git push origin main` (para enviar los archivos a GitHub)


> **Nota para artistas:** Si prefieres evitar la terminal en el día a día, una vez que hayas hecho el Paso 3 y 4, puedes descargar **[GitHub Desktop](https://desktop.github.com/)**. Es una interfaz visual que te permitirá ver tus archivos modificados y hacer los "commits" y "pushes" (Paso 5) simplemente haciendo clic en botones.
