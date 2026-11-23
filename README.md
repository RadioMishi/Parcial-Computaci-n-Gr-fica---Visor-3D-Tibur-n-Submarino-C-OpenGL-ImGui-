# Tiburon en el escenario submarino - Visor 3D con OpenGL

Visor 3D en C++ y OpenGL 3.3 que carga modelos glTF y muestra un tiburón nadando por una curva de Bezier dentro de un escenario submarino, con iluminación Phong y una interfaz hecha con Dear ImGui.

**Autor: Santiago Castro Puentes 6014317
Curso:** Computación Grafica - Parcial
**Video explicativo:** https://youtu.be/k4UkPDdsQ8w

## Características

* Carga de modelos **glTF 2.0** (`.gltf` + `.bin` + texturas) con TinyGLTF.
* **Matrices** de modelo, vista y proyección con GLM: traslación, rotación y escala de los objetos, y camara con `lookAt`.
* **Curva de Bezier** cubica por tramos (4 tramos encadenados con tangentes continuas) que define el recorrido del tiburón de A a B. La curva se dibuja en rojo.
* El tiburón se **orienta según la tangente** de la curva y puede girar 360 grados sobre su eje.
* **Iluminación Phong** (ambiental, difusa y especular con vector reflejado) con una luz puntual que sigue al tiburón, mas luz de sol y niebla submarina.
* **Entradas de teclado y panel de controles con Dear ImGui**.
* Soporte de materiales opacos, con transparencia (BLEND) y con recorte (MASK).

## Controles

|Entrada|Accion|
|-|-|
|W / A / S / D|Mover la camara|
|Q / E|Bajar / subir la camara|
|Clic derecho + mouse|Mirar alrededor|
|Espacio|Pausar el recorrido|
|R|Reiniciar el recorrido|
|Esc|Salir|

El panel **Controles** permite cambiar la velocidad, activar ida y vuelta, el giro de 360 grados, el tamaño del tiburón, los parámetros de Phong (ambiental, difusa, especular, brillo, color de la luz), la niebla, el color del agua y mostrar u ocultar la curva, la luz, el escenario y las transparencias.

## Arquitectura del software

Todo el programa esta en `Main.cpp`, organizado en módulos:

|Modulo|Responsabilidad|
|-|-|
|**Shaders**|Vertex shader (transforma posición y normal) y fragment shader (Phong, sol, niebla, transparencia).|
|**Carga de glTF** (`Model3D`, `loadModel`, `processNode`, `addPrimitive`)|Lee el archivo glTF, recorre los nodos aplicando sus transformaciones y sube vertices e indices a OpenGL (VAO, VBO, EBO).|
|**Materiales y texturas** (`materialInfo`, `uploadTextures`)|Lee color base, textura y modo de transparencia de cada material y sube las texturas a la GPU.|
|**Curva de Bezier** (`pathPoint`)|Calcula posición y tangente a lo largo de la curva en función del tiempo.|
|**Camara**|Camara libre controlada por teclado y mouse, con matrices `view` y `projection`.|
|**Interfaz** (Dear ImGui)|Panel de controles y lectura del teclado.|
|**Bucle principal**|Actualiza entradas y animación, dibuja la escena en dos pasadas (opacos y transparentes) y luego la interfaz.|

Flujo de cada cuadro:

1. Leer entradas (teclado y mouse) con ImGui.
2. Avanzar el tiempo y calcular la posición y orientación del tiburón sobre la curva.
3. Enviar matrices, luz y niebla a los shaders.
4. Dibujar la curva, el escenario y el tiburón (opacos), y después las transparencias.
5. Dibujar el panel de ImGui y mostrar el cuadro.

## Estructura del proyecto

```
Main.cpp            código principal
glad.c              cargador de OpenGL
tiny\_gltf.h         lector de glTF
json.hpp            dependencia de TinyGLTF
stb\_image.h         lectura de imágenes
glm/                librería de matemáticas
Imgui/              Dear ImGui y sus backends
Librerias/          GLFW (include y glfw3.lib), glad y KHR
scene.gltf, scene.bin, textures/     modelo del tiburón
escenario/          escenario submarino (scene.gltf, scene.bin, textures/)
```

## Como compilar

1. Abrir la solución en **Visual Studio** (C++17, plataforma **x64**).
2. Agregar al proyecto `Main.cpp`, `glad.c` y todos los `.cpp` de la carpeta `Imgui`.
3. En las propiedades del proyecto:

   * **C/C++ > General > Directorios de inclusion adicionales:** `$(ProjectDir);$(ProjectDir)Librerias\\include`
   * **Vinculador > General > Directorios de bibliotecas adicionales:** `$(ProjectDir)Librerias`
   * **Vinculador > Entrada > Dependencias adicionales:** `glfw3.lib;opengl32.lib;%(AdditionalDependencies)`
4. Compilar y ejecutar. Los archivos del modelo deben estar en la carpeta del proyecto (directorio de trabajo).

## Librerías utilizadas

GLFW, GLAD, GLM, TinyGLTF, stb\_image y Dear ImGui.

## 

