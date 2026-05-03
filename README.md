# geom

A study repository for implementing 2D geometric algorithms in **pure C** and visualizing them with **ImGui**.

## Direction

- **Pure C** — minimal external dependencies, focus on the algorithms themselves
- **MISRA C (key rules only)** — full compliance is overkill; cherry-pick the rules that meaningfully contribute to safety and readability
- **2D only** — restricted to the plane; 3D is out of scope
- **Numerical robustness** — degenerate cases and floating-point error handled head-on
  - Shewchuk, *Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric Predicates* (1997)
- **Visualization** — ImGui (GLFW + OpenGL3 backend) demo app for step-by-step inspection of each algorithm

## Roadmap

| Topic | Algorithm / Reference | Notes |
|-------|----------------------|-------|
| Convex Hull | Andrew's monotone chain | First target |
| Delaunay Triangulation | Both Incremental and Divide & Conquer | |
| Constrained Delaunay (CDT) | | Foundation for NavMesh |
| Voronoi Diagram | Derived as the dual of Delaunay | |
| Minimum Enclosing Circle | Welzl | expected O(n) |
| Point Location | Delaunay triangulation + Persistent Search Tree (Sarnak–Tarjan) | O(log n) query |
| Boolean Operations | Martínez et al., *A new algorithm for computing Boolean operations on general polygons* | union / intersection / difference / xor |
| NavMesh | CDT + Point Location + Funnel | with path smoothing |
| Half-Plane Intersection | Divide & conquer | O(n log n) |
| Robust Predicates | Shewchuk's adaptive predicates | foundation for the algorithms above |

> Tests are managed in a separate repository.

## Build

CMake-based.

```sh
cmake -B build
cmake --build build
```

_(detailed options TBD)_

## References

- de Berg, van Kreveld, Overmars, Schwarzkopf — *Computational Geometry: Algorithms and Applications*
- Shewchuk — *Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric Predicates* (1997)
- Martínez, Rueda, Feito — *A new algorithm for computing Boolean operations on general polygons* (2009)
- Sarnak, Tarjan — *Planar Point Location Using Persistent Search Trees* (1986)
