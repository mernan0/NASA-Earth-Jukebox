# Earth Jukebox | NASA Space Apps Challenge

An interactive C++ audio-visual application that transforms real Earth science data—specifically ocean currents and sea surface temperatures from the ECCO2 model—into dynamic, real-time synthesized soundscapes and an immersive graphical control interface.

Built with **C++** and **SFML** for the **NASA Space Apps Challenge**.

---

## Key Features

* **Universal Image Sonification:** The core audio engine can take *any* image, reduce and normalize its pixel values, and translate them into a continuous audio stream.
* **Dynamic Data Ingestion:** The program automatically scans the `Data/` folder for image inputs (`Image_1.png`, `Image_2.png`, etc.), making it easy to swap out or add custom datasets.
* **Mission Control UI:** Features an atmospheric start screen, a live fluid-flow simulation workspace, and an interactive media deck gallery.
* **Live Audio-Visualizers:** Integrated real-time RMS waveform bars and progress scrubbers that react dynamically during playback.
* **Seamless Media Switcher:** A bottom thumbnail playback bar allowing users to switch between datasets instantly without leaving the full-screen view.

---

## How It Works (The Data Pipeline)

1. **Pixel Processing & Reduction:** The application reads image files from the `Data/` folder, extracting color matrices and reducing them into manageable pixel strips.
2. **Normalization & Mapping:** 
   * **Brightness** is normalized and mapped to **frequency** (ranging from 220Hz to 880Hz).
   * **Color Saturation** is mapped to **amplitude/volume** levels.
3. **Audio Synthesis:** The engine synthesizes these mapped values into PCM audio buffers (`sf::SoundBuffer`), generating a custom soundscape unique to each image's visual topography.

---

## Tech Stack & Requirements

* **Language:** C++ (Standard C++17 or higher)
* **Graphics & Audio Library:** SFML (Simple and Fast Multimedia Library) v2.6 / v3 compatible
* **Build System:** CMake
* **Compiler:** MinGW / GCC / MSVC

---

## Team Contributions


* **Mernan Elkassed** – Core Architecture, Data Pipeline, System Integration & Logic
* **Menna Yasser & Yasmine Omar** – Audio Engineering, Sound Generation Algorithms & Audio DSP
* **Wafaa Mohamed, Alaa Yasser, & Amina Ebrahim** – UI/UX Layout Planning & Graphical Interface Development (GUI)

---

## Getting Started

1. **Clone the Repository:**
   ```bash
   git clone [https://github.com/mernan0/NASA-Earth-Jukebox.git](https://github.com/mernan0/NASA-Earth-Jukebox.git)
   cd NASA-Earth-Jukebox