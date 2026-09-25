# DirectX – Tessellation and Displacement Mapping

This project was developed as part of the **Graphics API Programming** course at the Silesian University of Technology. The objective of this lab was to implement the **tessellation stage** in DirectX 12, including static and dynamic tessellation, texture mapping with UV coordinates, and displacement mapping using a height map.

## Objective

- Integrate the **tessellation stage** using Hull and Domain shaders
- Apply **static tessellation** to subdivide a quad mesh into finer geometry
- Implement **dynamic tessellation** based on camera distance
- Generate and interpolate **UV texture coordinates** in the domain shader
- Load and apply **height maps** to displace geometry via displacement mapping
- Use **constant buffers** to pass camera data and matrices to shaders
- Bind textures using **Shader Resource Views (SRV)** and apply them in HLSL

## Technical Stack

- **API**: DirectX 12
- **Language**: C++17
- **Shaders**: HLSL (Shader Model 5.0+)
- **IDE**: Visual Studio 2019+
- **Platform**: Windows 10/11

## How to Run

1. Open the Visual Studio solution file (`*.sln`)
2. Build the project using `Ctrl+Shift+B`
3. Run the application using `F5`

### Controls

- **Mouse (Left Click + Drag)**: Orbit the camera
- **Mouse (Right Click + Drag)**: Zoom the camera

## Project Structure

```
├── shader.fx             # HLSL shader program
├── GeometryHelper.h      # Cube geometry and vertex color logic
├── RenderWidget.cpp      # Rendering logic and resource binding
├── System.cpp            # Application window and event loop
├── .gitignore            # Excludes build artifacts and cache
└── README.md             # Project documentation
```

## Results

<p align="center">
  <img src="https://github.com/user-attachments/assets/d2cb2762-9cc2-4b5a-8f7e-a8aa1df58c23" alt="Tessellation Result" width="400"/>
</p>

> **Note**: The tessellation level increases near the camera and decreases with distance. Textures are mapped using UV coordinates interpolated in the domain shader. Height map displaces the mesh vertically, simulating surface detail.

## Academic Context

This project was developed as part of laboratory exercises for the course:

> **Graphics API Programming (DirectX)**  
> Silesian University of Technology – Faculty of Automatic Control, Electronics and Computer Science  
> Supervised by: mgr inż. Aleksandra Szymczak

---

> **Technical Notes**:
> - Tessellation is enabled by using 4-control-point patches and setting topology to `D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST`
> - Constant Hull Shader dynamically modifies tessellation factors based on camera distance
> - `SampleLevel()` is used in the Domain Shader to apply displacement mapping using grayscale values from the texture
> - The pipeline uses a **wireframe mode** for visualization, but can be switched to solid rendering for final output

## Author

**inż. Alan Pawleta**  
Silesian University of Technology  
Faculty of Automatic Control, Electronics and Computer Science
