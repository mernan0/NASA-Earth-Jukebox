#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>

using namespace std;

constexpr float W = 1200.f;
constexpr float H = 750.f;
constexpr float PI = 3.14159265f;
constexpr unsigned SAMPLE_RATE = 44100;
constexpr float DS_DUR = 4.f;         // length of each dataset sound (seconds)

// ---------------------------------------------------------------
// Sound Teammate's Constants & Functions
// ---------------------------------------------------------------
const float MIN_FREQUENCY = 220.0f;
const float MAX_FREQUENCY = 880.0f;
const float MIN_AMPLITUDE = 0.08f;
const float MAX_AMPLITUDE = 0.35f;
const int MAX_PIXELS = 256;
const float PIXEL_DURATION = 0.025f;

static float normalizeValue(float value, float minValue, float maxValue) {
    if (maxValue <= minValue) return 0.0f;
    return (value - minValue) / (maxValue - minValue);
}

static float getBrightness(sf::Color pixel) {
    return (0.299f * pixel.r + 0.587f * pixel.g + 0.114f * pixel.b);
}

static float getSaturation(sf::Color pixel) {
    float maximum = max({pixel.r, pixel.g, pixel.b});
    float minimum = min({pixel.r, pixel.g, pixel.b});
    if (maximum == 0) return 0.0f;
    return (maximum - minimum) / maximum;
}

static float calculateFrequency(float brightness) {
    float normalized = normalizeValue(brightness, 0.0f, 255.0f);
    return MIN_FREQUENCY + normalized * (MAX_FREQUENCY - MIN_FREQUENCY);
}

static float calculateAmplitude(float saturation) {
    return MIN_AMPLITUDE + saturation * (MAX_AMPLITUDE - MIN_AMPLITUDE);
}

static vector<sf::Color> reducePixels(const vector<sf::Color>& pixels) {
    vector<sf::Color> reduced;
    if (pixels.empty()) return reduced;
    int totalPixels = static_cast<int>(pixels.size());
    int newSize = min(totalPixels, MAX_PIXELS);
    for (int i = 0; i < newSize; i++) {
        int start = i * totalPixels / newSize;
        int end = (i + 1) * totalPixels / newSize;
        int red = 0, green = 0, blue = 0;
        int count = end - start;
        if (count <= 0) continue;
        for (int j = start; j < end; j++) {
            red += pixels[j].r;
            green += pixels[j].g;
            blue += pixels[j].b;
        }
        reduced.push_back(sf::Color(red / count, green / count, blue / count));
    }
    return reduced;
}

static vector<sf::Int16> generateSoundForSingleImage(const vector<sf::Color>& inputPixels) {
    vector<sf::Int16> samples;
    if (inputPixels.empty()) {
        return vector<sf::Int16>(static_cast<std::size_t>(DS_DUR * SAMPLE_RATE), 0);
    }

    vector<sf::Color> pixels = reducePixels(inputPixels);
    vector<float> soundData;
    float previousFrequency = MIN_FREQUENCY;
    float previousAmplitude = MIN_AMPLITUDE;
    const int samplesPerPixel = static_cast<int>(SAMPLE_RATE * PIXEL_DURATION);
    float phase = 0.0f;

    for (std::size_t j = 0; j < pixels.size(); j++) {
        float brightness = getBrightness(pixels[j]);
        float saturation = getSaturation(pixels[j]);
        float frequency = calculateFrequency(brightness);
        float amplitude = calculateAmplitude(saturation);
        for (int k = 0; k < samplesPerPixel; k++) {
            float progress = static_cast<float>(k) / samplesPerPixel;
            float currentFrequency = previousFrequency + progress * (frequency - previousFrequency);
            float currentAmplitude = previousAmplitude + progress * (amplitude - previousAmplitude);
            float sample = currentAmplitude * sin(phase);
            soundData.push_back(sample);
            phase += 2.0f * PI * currentFrequency / SAMPLE_RATE;
            if (phase >= 2.0f * PI) phase -= 2.0f * PI;
        }
        previousFrequency = frequency;
        previousAmplitude = amplitude;
    }

    int fadeSamples = static_cast<int>(SAMPLE_RATE * 0.02f);
    if (fadeSamples * 2 > static_cast<int>(soundData.size())) {
        fadeSamples = static_cast<int>(soundData.size() / 4);
    }
    for (std::size_t i = 0; i < soundData.size(); i++) {
        float fade = 1.0f;
        if (static_cast<int>(i) < fadeSamples) {
            fade = static_cast<float>(i) / fadeSamples;
        }
        if (static_cast<int>(i) >= static_cast<int>(soundData.size()) - fadeSamples) {
            float fadeOut = static_cast<float>(soundData.size() - i) / fadeSamples;
            fade = min(fade, fadeOut);
        }
        float finalSample = soundData[i] * fade;
        finalSample = max(-1.0f, min(1.0f, finalSample));
        samples.push_back(static_cast<sf::Int16>(finalSample * 32767));
    }
    return samples;
}

// ---------------------------------------------------------------
// GUI Helpers (SFML 2.6.1 Syntax Updates)
// ---------------------------------------------------------------
static sf::Color col(int r, int g, int b, int a = 255)
{
    auto c = [](int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); };
    return sf::Color(c(r), c(g), c(b), c(a));
}

static void centerText(sf::Text& t, float x, float y)
{
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
    t.setPosition(x, y);
}

static float hsh(int i, int salt)
{
    float v = std::sin(i * 12.9898f + salt * 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

static std::mt19937 g_rng(1234);
static float frand(float a, float b)
{
    return std::uniform_real_distribution<float>(a, b)(g_rng);
}

static sf::SoundBuffer makeBuffer(const std::vector<std::int16_t>& samples)
{
    sf::SoundBuffer buffer;
    if (!buffer.loadFromSamples(samples.data(), samples.size(), 1, SAMPLE_RATE))
    {
        std::cout << "Failed to create sound buffer\n";
    }
    return buffer;
}

static sf::SoundBuffer makeNote(float freq, float duration)
{
    std::size_t n = static_cast<std::size_t>(duration * SAMPLE_RATE);
    std::vector<std::int16_t> s(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;
        float env = std::exp(-3.5f * t / duration) * std::min(1.f, t * 200.f);
        float v = std::sin(2.f * PI * freq * t)
            + 0.4f * std::sin(2.f * PI * freq * 2.f * t)
            + 0.15f * std::sin(2.f * PI * freq * 3.f * t);
        s[i] = static_cast<std::int16_t>(v * env * 0.35f * 32767.f);
    }
    return makeBuffer(s);
}

static sf::SoundBuffer makeWhoosh(float duration)
{
    std::size_t n = static_cast<std::size_t>(duration * SAMPLE_RATE);
    std::vector<std::int16_t> s(n);
    float phase = 0.f;
    for (std::size_t i = 0; i < n; ++i)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;
        float freq = 1400.f - 1000.f * (t / duration);
        phase += 2.f * PI * freq / SAMPLE_RATE;
        float env = std::sin(PI * t / duration);
        s[i] = static_cast<std::int16_t>(std::sin(phase) * env * 0.22f * 32767.f);
    }
    return makeBuffer(s);
}

static sf::SoundBuffer makeAmbient()
{
    std::size_t n = static_cast<std::size_t>(4.f * SAMPLE_RATE);
    std::vector<std::int16_t> s(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;
        float v = 0.5f * std::sin(2.f * PI * 55.f * t)
            + 0.3f * std::sin(2.f * PI * 82.5f * t)
            + 0.2f * std::sin(2.f * PI * 110.25f * t);
        float trem = 0.75f + 0.25f * std::sin(2.f * PI * 0.5f * t);
        s[i] = static_cast<std::int16_t>(v * trem * 0.4f * 32767.f);
    }
    return makeBuffer(s);
}

static float rmsAt(const std::vector<std::int16_t>& s, std::size_t start, std::size_t len)
{
    if (start >= s.size()) return 0.f;
    std::size_t end = std::min(start + len, s.size());
    double sum = 0.0;
    for (std::size_t i = start; i < end; ++i)
    {
        double v = s[i] / 32768.0;
        sum += v * v;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - start)));
}

struct Star
{
    float x, y, r, phase, twinkle, drift;
};

struct Planet
{
    float x, y, r, speed, tilt;
    sf::Color color;
    bool ring;
};

struct ShootingStar
{
    sf::Vector2f pos, vel;
    float life;
};

struct Pulse
{
    sf::Vector2f pos;
    float age;
    sf::Color color;
};

struct Dataset
{
    std::string name;
    std::string file;
    std::string caps[3];
    sf::Color c1, c2;
    std::vector<std::int16_t> pcm;
    float norm = 1.f;
    sf::SoundBuffer buf;
    sf::Texture tex;
    bool hasTex = false;
};

static void drawThumb(sf::RenderTarget& rt, int kind, float x, float y, float w, float h,
    sf::Color c1, sf::Color c2, float t)
{
    sf::VertexArray g(sf::TriangleStrip, 4);
    g[0].position = sf::Vector2f(x, y);          g[0].color = c1;
    g[1].position = sf::Vector2f(x + w, y);      g[1].color = c1;
    g[2].position = sf::Vector2f(x, y + h);      g[2].color = c2;
    g[3].position = sf::Vector2f(x + w, y + h);  g[3].color = c2;
    rt.draw(g);

    sf::CircleShape c(1.f, 24);
    switch (kind)
    {
    case 0:
        for (int i = 0; i < 6; ++i)
        {
            float r = 10.f + 10.f * hsh(i, 1);
            c.setRadius(r);
            c.setOrigin(r, r);
            c.setPosition(x + w * (0.1f + 0.8f * hsh(i, 2)) + 6.f * std::sin(t + i),
                          y + h * (0.2f + 0.6f * hsh(i, 3)));
            c.setFillColor(col(255, 200, 80, 90));
            rt.draw(c);
        }
        break;
    case 1:
    {
        sf::RectangleShape drop(sf::Vector2f(1.5f, 12.f));
        drop.setFillColor(col(200, 225, 255, 150));
        for (int i = 0; i < 36; ++i)
        {
            float py = std::fmod(hsh(i, 2) * h + t * (90.f + 60.f * hsh(i, 3)), h - 12.f);
            drop.setPosition(x + w * hsh(i, 1), y + py);
            rt.draw(drop);
        }
        break;
    }
    case 2:
        for (int i = 0; i < 28; ++i)
        {
            float r = (4.f + 7.f * hsh(i, 1)) * (1.f + 0.1f * std::sin(t * 2.f + i));
            c.setRadius(r);
            c.setOrigin(r, r);
            c.setPosition(x + w * (0.05f + 0.9f * hsh(i, 2)), y + h * (0.15f + 0.7f * hsh(i, 3)));
            c.setFillColor(col(60 + int(80 * hsh(i, 4)), 150 + int(80 * hsh(i, 5)), 60, 130));
            rt.draw(c);
        }
        break;
    case 3:
        for (int i = 0; i < 70; ++i)
        {
            float r = 1.f + 1.5f * hsh(i, 1);
            float a = 0.5f + 0.5f * std::sin(t * (1.f + 2.f * hsh(i, 4)) + i);
            c.setRadius(r);
            c.setOrigin(r, r);
            c.setPosition(x + 4.f + (w - 8.f) * hsh(i, 2), y + 4.f + (h - 8.f) * hsh(i, 3));
            c.setFillColor(col(255, 225, 140, int(80 + 175 * a)));
            rt.draw(c);
        }
        break;
    case 4:
        for (int j = 0; j < 6; ++j)
        {
            sf::VertexArray wave(sf::LineStrip);
            for (int k = 0; k <= 40; ++k)
            {
                sf::Vertex v;
                v.position = sf::Vector2f(x + w * k / 40.f,
                                          y + h * (0.2f + 0.12f * j) + 7.f * std::sin(k * 0.5f + t * 2.f + j));
                v.color = col(255, 255, 255, 110);
                wave.append(v);
            }
            rt.draw(wave);
        }
        break;
    case 5:
        c.setPointCount(6);
        for (int i = 0; i < 12; ++i)
        {
            float r = 8.f + 12.f * hsh(i, 1);
            c.setRadius(r);
            c.setOrigin(r, r);
            c.setPosition(x + w * (0.12f + 0.76f * hsh(i, 2)), y + h * (0.22f + 0.56f * hsh(i, 3)));
            c.setRotation(t * 10.f + i * 17.f);
            c.setFillColor(col(255, 255, 255, 100));
            rt.draw(c);
        }
        break;
    }
}

enum class Screen { Start, Workspace, Gallery };

int main()
{
    // ---------------- Your Data Input Section ----------------
    vector<vector<sf::Color>> ImagesPixels;
    int imgIdx = 1;
    sf::Image image;
    while(imgIdx < 7){
        string filePath = "Data/Image_" + to_string(imgIdx) + ".png";
        if (image.loadFromFile(filePath)) {
            unsigned int width = image.getSize().x;
            unsigned int height = image.getSize().y;
            vector<sf::Color> tempPixels;
            tempPixels.reserve(width * height);
            for (unsigned int y = 0 ; y < height ; ++y) {
                for (unsigned int x = 0 ; x < width ; ++x) {
                    tempPixels.push_back(image.getPixel(x, y));
                }
            }
            ImagesPixels.push_back(tempPixels);
        } else {
            ImagesPixels.push_back({}); // Empty fallback if image missing
        }
        imgIdx++;
    }

    // Create Window (SFML 2.6.1 VideoMode format)
    sf::RenderWindow window(sf::VideoMode(1200u, 750u), "Space Walkie Talkie");
    window.setFramerateLimit(60);

    // Font (SFML 2 loadFromFile)
    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/arial.ttf"))
    {
        std::cout << "Font loading failed!\n";
        return -1;
    }

    // ---------------- Audio ----------------
    const float noteFreqs[5] = { 523.25f, 587.33f, 659.25f, 783.99f, 880.00f };
    const sf::Color noteColors[5] = {
        col(120, 230, 255), col(160, 140, 255), col(255, 160, 220),
        col(255, 220, 130), col(130, 255, 190)
    };

    std::vector<sf::SoundBuffer> notes;
    for (float f : noteFreqs) notes.push_back(makeNote(f, 1.2f));

    sf::SoundBuffer whooshBuf = makeWhoosh(0.9f);
    sf::SoundBuffer ambientBuf = makeAmbient();

    sf::Sound ambient(ambientBuf);
    ambient.setLoop(true);
    ambient.setVolume(30.f);
    ambient.play();
    bool muted = false;

    std::vector<sf::Sound> pool(8, sf::Sound(notes[0]));
    std::size_t poolIndex = 0;

    auto playSound = [&](const sf::SoundBuffer& buf, float pitch, float volume)
    {
        sf::Sound& s = pool[poolIndex];
        poolIndex = (poolIndex + 1) % pool.size();
        s.setBuffer(buf);
        s.setPitch(pitch);
        s.setVolume(muted ? 0.f : volume);
        s.play();
    };

    // ---------------- NASA datasets (Connected to your ImagesPixels) ----------------
    std::vector<Dataset> datasets;
    datasets.reserve(6);

    auto addDataset = [&](const std::string& name, const std::string& file,
        const std::string& c0, const std::string& c1s, const std::string& c2s,
        sf::Color top, sf::Color bottom, std::vector<std::int16_t> pcm)
    {
        datasets.emplace_back();
        Dataset& d = datasets.back();
        d.name = name;
        d.file = file;
        d.caps[0] = c0; d.caps[1] = c1s; d.caps[2] = c2s;
        d.c1 = top; d.c2 = bottom;
        d.pcm = std::move(pcm);
        d.buf = makeBuffer(d.pcm);

        float peak = 0.001f;
        for (std::size_t idx = 0; idx < d.pcm.size(); idx += 2048)
            peak = std::max(peak, rmsAt(d.pcm, idx, 4096));
        d.norm = 1.f / (peak * 0.85f);

        for (const char* ext : { ".png", ".jpg" })
        {
            if (d.tex.loadFromFile("images/" + file + ext))
            {
                d.tex.setSmooth(true);
                d.hasTex = true;
                break;
            }
        }
    };

    auto getPcm = [&](int index) {
        if (index < static_cast<int>(ImagesPixels.size()) && !ImagesPixels[index].empty()) {
            return generateSoundForSingleImage(ImagesPixels[index]);
        }
        return std::vector<std::int16_t>(static_cast<std::size_t>(DS_DUR * SAMPLE_RATE), 0);
    };

    addDataset("Surface Temperature", "temperature",
        "Polar cold: low, calm tone", "Temperate climate: steady tone", "Desert heat: high, bright tone",
        col(40, 70, 200), col(230, 80, 50), getPcm(0));
    addDataset("Global Rainfall", "rainfall",
        "Dry land: sparse, slow drops", "Showers: steady rhythm", "Tropical storm: dense, fast rain",
        col(30, 50, 100), col(80, 130, 190), getPcm(1));
    addDataset("Vegetation (Greenness)", "vegetation",
        "Barren land: thin, quiet hum", "Grassland: soft, simple chord", "Rainforest: full, rich harmony",
        col(20, 80, 40), col(90, 170, 60), getPcm(2));
    addDataset("Earth at Night", "night_lights",
        "Remote areas: dark, few notes", "Towns: gentle sparkle", "Megacity: bright, dense lights",
        col(5, 5, 25), col(30, 30, 80), getPcm(3));
    addDataset("Ocean Waves", "ocean",
        "Calm sea: slow, low swell", "Rolling waves: rising swell", "Storm surge: powerful wave",
        col(10, 50, 130), col(20, 140, 190), getPcm(4));
    addDataset("Arctic Sea Ice", "sea_ice",
        "Thin ice: faint, distant chimes", "Seasonal ice: clear, ringing bells", "Thick ice cap: bright, rich echo",
        col(90, 140, 200), col(210, 235, 250), getPcm(5));

    std::vector<sf::Sound> dsSounds;
    dsSounds.reserve(datasets.size());
    for (auto& d : datasets)
    {
        dsSounds.emplace_back(d.buf);
        dsSounds.back().setVolume(90.f);
    }
    std::vector<float> smooth(datasets.size(), 0.f);

    auto stopAllDataSounds = [&]()
    {
        for (auto& s : dsSounds) s.stop();
    };

    // ---------------- Scene data ----------------
    std::vector<Star> stars;
    for (int idx = 0; idx < 170; ++idx)
    {
        stars.push_back({ frand(0, W), frand(0, H), frand(0.8f, 2.4f),
                          frand(0, 6.28f), frand(1.f, 3.f), frand(2.f, 12.f) });
    }

    std::vector<Planet> planets = {
        { 1000.f, 130.f, 48.f, 8.f,  -20.f, col(210, 120, 90),  false },
        { 180.f,  610.f, 70.f, 5.f,  -15.f, col(120, 150, 225), true },
        { 930.f,  620.f, 26.f, 12.f,   0.f, col(235, 205, 125), false }
    };

    std::vector<ShootingStar> shooting;
    std::vector<Pulse> pulses;

    float shootTimer = frand(1.5f, 3.f);
    float pulseTimer = frand(0.6f, 1.2f);
    float satAngle = 0.f;
    float t = 0.f;

    sf::VertexArray bg(sf::TriangleStrip, 4);
    bg[0].position = sf::Vector2f(0.f, 0.f);
    bg[1].position = sf::Vector2f(W, 0.f);
    bg[2].position = sf::Vector2f(0.f, H);
    bg[3].position = sf::Vector2f(W, H);

    sf::CircleShape dot(1.f, 24);

    sf::Text title;
    title.setFont(font);
    title.setString("Space Walkie Talkie");
    title.setCharacterSize(65);
    title.setFillColor(sf::Color(90, 220, 220));
    centerText(title, W / 2.f, 230.f);

    sf::Text subtitle;
    subtitle.setFont(font);
    subtitle.setString("Turn Earth's Data Into Sound");
    subtitle.setCharacterSize(24);
    subtitle.setFillColor(sf::Color(190, 200, 220));
    centerText(subtitle, W / 2.f, 320.f);

    sf::RectangleShape startButton(sf::Vector2f(260.f, 70.f));
    startButton.setOrigin(130.f, 35.f);
    startButton.setPosition(W / 2.f, 450.f);
    startButton.setFillColor(sf::Color(20, 145, 160));
    startButton.setOutlineThickness(2.f);
    startButton.setOutlineColor(sf::Color(150, 240, 245, 160));

    sf::Text buttonText;
    buttonText.setFont(font);
    buttonText.setString("START EXPLORING");
    buttonText.setCharacterSize(20);
    buttonText.setFillColor(sf::Color::White);
    centerText(buttonText, W / 2.f, 450.f);

    sf::Text welcome;
    welcome.setFont(font);
    welcome.setString("WELCOME TO YOUR WORKSPACE");
    welcome.setCharacterSize(38);
    welcome.setFillColor(sf::Color::White);
    centerText(welcome, W / 2.f, 95.f);

    const char* lines[3] = {
        "Space Walkie Talkie turns NASA's Earth data into sound.",
        "Temperature, rainfall, vegetation and light become tones and rhythms,",
        "so blind and visually impaired explorers can hear our planet."
    };
    sf::Text para1, para2, para3;
    para1.setFont(font); para1.setString(lines[0]); para1.setCharacterSize(24);
    para2.setFont(font); para2.setString(lines[1]); para2.setCharacterSize(22);
    para3.setFont(font); para3.setString(lines[2]); para3.setCharacterSize(22);
    para1.setFillColor(sf::Color(90, 220, 220));
    para2.setFillColor(sf::Color(200, 210, 230));
    para3.setFillColor(sf::Color(200, 210, 230));
    centerText(para1, W / 2.f, 175.f);
    centerText(para2, W / 2.f, 215.f);
    centerText(para3, W / 2.f, 247.f);

    const float cardCX = W / 2.f;
    const float cardCY = 560.f;
    const sf::Vector2f cardSize(360.f, 120.f);

    sf::Text hint;
    hint.setFont(font);
    hint.setString("ESC: back    M: mute");
    hint.setCharacterSize(16);
    hint.setFillColor(sf::Color(150, 160, 180));
    centerText(hint, W / 2.f, 715.f);

    auto nasaCardRect = [&]()
    {
        return sf::FloatRect(cardCX - cardSize.x / 2.f, cardCY - cardSize.y / 2.f, cardSize.x, cardSize.y);
    };
    auto galleryRect = [&](int idx)
    {
        int c = idx % 3, r = idx / 3;
        return sf::FloatRect(60.f + c * 370.f, 105.f + r * 295.f, 340.f, 280.f);
    };

    Screen screen = Screen::Start;
    sf::Clock clock;

    // ---------------- Main Loop ----------------
    while (window.isOpen())
    {
        float dt = std::min(clock.restart().asSeconds(), 0.05f);
        t += dt;

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }

            if (event.type == sf::Event::KeyPressed)
            {
                if (event.key.code == sf::Keyboard::Escape)
                {
                    if (screen == Screen::Gallery) { stopAllDataSounds(); screen = Screen::Workspace; }
                    else if (screen == Screen::Workspace) { screen = Screen::Start; }
                }
                if (event.key.code == sf::Keyboard::M)
                {
                    muted = !muted;
                    ambient.setVolume(muted ? 0.f : 30.f);
                    for (auto& s : dsSounds) s.setVolume(muted ? 0.f : 90.f);
                }
            }

            if (event.type == sf::Event::MouseButtonPressed)
            {
                if (event.mouseButton.button == sf::Mouse::Left)
                {
                    sf::Vector2f position(
                        static_cast<float>(event.mouseButton.x),
                        static_cast<float>(event.mouseButton.y)
                    );

                    if (screen == Screen::Start && startButton.getGlobalBounds().contains(position))
                    {
                        screen = Screen::Workspace;
                        playSound(notes[4], 1.f, 70.f);
                        playSound(notes[2], 1.f, 60.f);
                    }
                    else if (screen == Screen::Workspace)
                    {
                        if (nasaCardRect().contains(position))
                        {
                            playSound(notes[3], 1.f, 60.f);
                            screen = Screen::Gallery;
                        }
                    }
                    else if (screen == Screen::Gallery)
                    {
                        for (std::size_t idx = 0; idx < datasets.size(); ++idx)
                        {
                            if (galleryRect(static_cast<int>(idx)).contains(position))
                            {
                                stopAllDataSounds();
                                dsSounds[idx].setVolume(muted ? 0.f : 90.f);
                                dsSounds[idx].play();
                                smooth[idx] = 0.f;
                            }
                        }
                    }
                }
            }
        }

        // ---------------- Update ----------------
        for (auto& s : stars)
        {
            s.x -= s.drift * dt;
            if (s.x < -5.f) { s.x = W + 5.f; s.y = frand(0, H); }
        }

        for (auto& p : planets)
        {
            p.x -= p.speed * dt;
            if (p.x < -250.f) p.x = W + 250.f;
        }

        satAngle += 0.35f * dt;

        shootTimer -= dt;
        if (shootTimer <= 0.f)
        {
            shooting.push_back({ sf::Vector2f(frand(W * 0.4f, W), frand(0.f, H * 0.35f)),
                                 sf::Vector2f(-750.f, 380.f), 0.9f });
            if (screen != Screen::Gallery)
                playSound(whooshBuf, frand(0.9f, 1.2f), 35.f);
            shootTimer = frand(2.5f, 5.f);
        }
        for (auto& s : shooting)
        {
            s.pos += s.vel * dt;
            s.life -= dt;
        }
        shooting.erase(std::remove_if(shooting.begin(), shooting.end(),
            [](const ShootingStar& s) { return s.life <= 0.f; }),
            shooting.end());

        pulseTimer -= dt;
        if (pulseTimer <= 0.f)
        {
            const Star& s = stars[static_cast<std::size_t>(frand(0.f, static_cast<float>(stars.size()) - 0.01f))];
            int k = static_cast<int>(frand(0.f, 4.99f));
            pulses.push_back({ sf::Vector2f(s.x, s.y), 0.f, noteColors[k] });
            if (screen != Screen::Gallery)
                playSound(notes[static_cast<std::size_t>(k)], 1.f, 45.f);
            pulseTimer = frand(0.7f, 1.6f);
        }
        for (auto& p : pulses) p.age += dt;
        pulses.erase(std::remove_if(pulses.begin(), pulses.end(),
            [](const Pulse& p) { return p.age > 1.2f; }),
            pulses.end());

        for (std::size_t idx = 0; idx < datasets.size(); ++idx)
        {
            float target = 0.f;
            if (dsSounds[idx].getStatus() == sf::Sound::Playing)
            {
                std::size_t off = static_cast<std::size_t>(dsSounds[idx].getPlayingOffset().asSeconds() * SAMPLE_RATE);
                target = std::clamp(rmsAt(datasets[idx].pcm, off, 4096) * datasets[idx].norm, 0.f, 1.f);
            }
            smooth[idx] += (target - smooth[idx]) * std::min(1.f, dt * 10.f);
        }

        auto mousePixel = sf::Mouse::getPosition(window);
        sf::Vector2f mousePosition(
            static_cast<float>(mousePixel.x),
            static_cast<float>(mousePixel.y)
        );

        bool hover = screen == Screen::Start && startButton.getGlobalBounds().contains(mousePosition);
        startButton.setFillColor(hover ? sf::Color(40, 190, 195) : sf::Color(20, 145, 160));
        startButton.setScale(hover ? sf::Vector2f(1.05f, 1.05f) : sf::Vector2f(1.f, 1.f));

        // ---------------- Draw ----------------
        window.clear();

        float k = 0.5f + 0.5f * std::sin(t * 0.15f);
        sf::Color top = col(int(14 + 16 * k), 6, int(44 + 20 * k));
        sf::Color bottom = col(4, int(34 + 20 * k), int(66 - 10 * k));
        bg[0].color = top;
        bg[1].color = top;
        bg[2].color = bottom;
        bg[3].color = bottom;
        window.draw(bg);

        auto glow = [&](float x, float y, float r, sf::Color c)
        {
            for (int idx = 0; idx < 5; ++idx)
            {
                float rr = r * (1.f - idx * 0.18f);
                sf::CircleShape g(rr, 64);
                g.setOrigin(rr, rr);
                g.setPosition(x, y);
                g.setFillColor(col(c.r, c.g, c.b, 10));
                window.draw(g);
            }
        };
        glow(300.f + 60.f * std::sin(t * 0.2f), 200.f, 280.f, col(140, 60, 200));
        glow(900.f + 50.f * std::cos(t * 0.17f), 520.f, 300.f, col(30, 170, 190));
        glow(600.f, 380.f + 40.f * std::sin(t * 0.13f), 240.f, col(60, 80, 210));

        for (const auto& s : stars)
        {
            float a = 0.5f + 0.5f * std::sin(t * s.twinkle + s.phase);
            dot.setRadius(s.r);
            dot.setOrigin(s.r, s.r);
            dot.setPosition(s.x, s.y);
            dot.setFillColor(col(255, 255, 255, int(70 + 185 * a)));
            window.draw(dot);
        }

        for (const auto& p : planets)
        {
            if (p.ring)
            {
                float rr = p.r * 1.8f;
                sf::CircleShape ring(rr, 80);
                ring.setOrigin(rr, rr);
                ring.setPosition(p.x, p.y);
                ring.setScale(1.f, 0.28f);
                ring.setRotation(p.tilt);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineThickness(4.f);
                ring.setOutlineColor(col(230, 200, 150, 150));
                window.draw(ring);
            }

            sf::CircleShape body(p.r, 64);
            body.setOrigin(p.r, p.r);
            body.setPosition(p.x, p.y);
            body.setFillColor(col(p.color.r, p.color.g, p.color.b, 215));
            window.draw(body);

            sf::CircleShape shade(p.r * 0.6f, 48);
            shade.setOrigin(p.r * 0.6f, p.r * 0.6f);
            shade.setPosition(p.x - p.r * 0.25f, p.y - p.r * 0.25f);
            shade.setFillColor(col(255, 255, 255, 40));
            window.draw(shade);
        }

        // Satellite
        {
            float sx = W / 2.f + 520.f * std::cos(satAngle);
            float sy = H / 2.f + 290.f * std::sin(satAngle);

            sf::RectangleShape sBody(sf::Vector2f(22.f, 12.f));
            sBody.setOrigin(11.f, 6.f);
            sBody.setPosition(sx, sy);
            sBody.setRotation((satAngle + PI / 2.f) * 180.f / PI);
            sBody.setFillColor(col(220, 225, 235));
            window.draw(sBody);

            for (int side = -1; side <= 1; side += 2)
            {
                sf::RectangleShape panel(sf::Vector2f(18.f, 8.f));
                panel.setOrigin(9.f, 4.f);
                float off = 20.f * side;
                panel.setPosition(sx + off * std::cos(satAngle + PI / 2.f),
                                    sy + off * std::sin(satAngle + PI / 2.f));
                panel.setRotation((satAngle + PI / 2.f) * 180.f / PI);
                panel.setFillColor(col(60, 140, 230));
                window.draw(panel);
            }
        }

        for (const auto& s : shooting)
        {
            float len = 130.f;
            sf::RectangleShape trail(sf::Vector2f(len, 2.5f));
            trail.setOrigin(len, 1.25f);
            trail.setPosition(s.pos);
            trail.setRotation(std::atan2(s.vel.y, s.vel.x) * 180.f / PI);
            trail.setFillColor(col(255, 255, 255, int(255 * (s.life / 0.9f))));
            window.draw(trail);
        }

        for (const auto& p : pulses)
        {
            float prog = p.age / 1.2f;
            float r = 6.f + prog * 70.f;
            sf::CircleShape ring(r, 48);
            ring.setOrigin(r, r);
            ring.setPosition(p.pos);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(2.f);
            ring.setOutlineColor(col(p.color.r, p.color.g, p.color.b, int(230 * (1.f - prog))));
            window.draw(ring);

            dot.setRadius(3.f);
            dot.setOrigin(3.f, 3.f);
            dot.setPosition(p.pos);
            dot.setFillColor(col(p.color.r, p.color.g, p.color.b, int(255 * (1.f - prog))));
            window.draw(dot);
        }

        // Screens
        if (screen == Screen::Start)
        {
            centerText(title, W / 2.f, 230.f + 6.f * std::sin(t * 1.5f));
            window.draw(title);
            window.draw(subtitle);
            window.draw(startButton);
            window.draw(buttonText);
        }
        else if (screen == Screen::Workspace)
        {
            window.draw(welcome);
            window.draw(para1);
            window.draw(para2);
            window.draw(para3);

            const int barCount = 56;
            for (int idx = 0; idx < barCount; ++idx)
            {
                float wave = (0.5f + 0.5f * std::sin(t * 3.f + idx * 0.35f))
                    * (0.6f + 0.4f * std::sin(t * 1.3f + idx * 0.12f));
                float h = 14.f + 80.f * wave;
                sf::RectangleShape bar(sf::Vector2f(10.f, h));
                bar.setOrigin(5.f, h / 2.f);
                bar.setPosition(208.f + idx * 14.f, 400.f);
                bar.setFillColor(col(40 + idx * 2, 190 - idx, 200, int(120 + 120 * wave)));
                window.draw(bar);
            }

            bool cardHover = nasaCardRect().contains(mousePosition);

            sf::RectangleShape card(cardSize);
            card.setOrigin(cardSize.x / 2.f, cardSize.y / 2.f);
            card.setPosition(cardCX, cardCY);
            card.setFillColor(col(255, 255, 255, cardHover ? 40 : 18));
            card.setOutlineThickness(cardHover ? 2.5f : 1.5f);
            card.setOutlineColor(col(90, 220, 220, cardHover ? 255 : 140));
            window.draw(card);

            sf::Text ct; ct.setFont(font); ct.setString("NASA DATA"); ct.setCharacterSize(28);
            ct.setFillColor(sf::Color(90, 220, 220));
            centerText(ct, cardCX, cardCY - 15.f);
            window.draw(ct);

            sf::Text ci; ci.setFont(font); ci.setString("Click to explore"); ci.setCharacterSize(18);
            ci.setFillColor(sf::Color(190, 200, 220));
            centerText(ci, cardCX, cardCY + 25.f);
            window.draw(ci);

            hint.setString("ESC: back    M: mute");
            centerText(hint, W / 2.f, 715.f);
            window.draw(hint);
        }
        else // Gallery
        {
            sf::Text gt; gt.setFont(font); gt.setString("NASA EARTH DATA"); gt.setCharacterSize(34);
            gt.setFillColor(sf::Color::White);
            centerText(gt, W / 2.f, 45.f);
            window.draw(gt);

            sf::Text gs; gs.setFont(font); gs.setString("Click any image to hear what the data sounds like"); gs.setCharacterSize(17);
            gs.setFillColor(sf::Color(90, 220, 220));
            centerText(gs, W / 2.f, 80.f);
            window.draw(gs);

            for (std::size_t idx = 0; idx < datasets.size(); ++idx)
            {
                Dataset& d = datasets[idx];
                sf::FloatRect r = galleryRect(static_cast<int>(idx));
                bool hov = r.contains(mousePosition);
                bool playing = dsSounds[idx].getStatus() == sf::Sound::Playing;
                float a = smooth[idx];

                sf::RectangleShape gcard(sf::Vector2f(r.width, r.height));
                gcard.setPosition(r.left, r.top);
                gcard.setFillColor(col(255, 255, 255, (hov || playing) ? 34 : 18));
                gcard.setOutlineThickness(playing ? 2.5f : 1.5f);
                gcard.setOutlineColor(playing ? col(130, 255, 230, int(140 + 115 * a))
                    : col(90, 220, 220, hov ? 220 : 120));
                window.draw(gcard);

                float ix = r.left + 10.f;
                float iy = r.top + 10.f;
                if (d.hasTex)
                {
                    sf::Sprite sp(d.tex);
                    auto ts = d.tex.getSize();
                    float sc = std::max(320.f / ts.x, 140.f / ts.y);
                    int cw = std::min(static_cast<int>(320.f / sc), static_cast<int>(ts.x));
                    int ch = std::min(static_cast<int>(140.f / sc), static_cast<int>(ts.y));
                    sp.setTextureRect(sf::IntRect((static_cast<int>(ts.x) - cw) / 2,
                                                    (static_cast<int>(ts.y) - ch) / 2, cw, ch));
                    sp.setScale(sc, sc);
                    sp.setPosition(ix, iy);
                    window.draw(sp);
                }
                else
                {
                    drawThumb(window, static_cast<int>(idx), ix, iy, 320.f, 140.f, d.c1, d.c2, t);
                }

                if (playing)
                {
                    sf::RectangleShape flash(sf::Vector2f(320.f, 140.f));
                    flash.setPosition(ix, iy);
                    flash.setFillColor(col(255, 255, 255, int(70 * a)));
                    window.draw(flash);
                }

                sf::Text nm; nm.setFont(font); nm.setString(d.name); nm.setCharacterSize(19);
                nm.setFillColor(sf::Color::White);
                centerText(nm, r.left + r.width / 2.f, r.top + 172.f);
                window.draw(nm);

                int level = a < 0.33f ? 0 : (a < 0.66f ? 1 : 2);
                std::string cap = playing ? d.caps[level] : std::string("Click to listen");
                sf::Color capCol = !playing ? col(150, 160, 180)
                    : (level == 0 ? col(150, 190, 230)
                        : (level == 1 ? col(90, 220, 220) : col(255, 220, 130)));
                sf::Text cp; cp.setFont(font); cp.setString(cap); cp.setCharacterSize(14);
                cp.setFillColor(capCol);
                centerText(cp, r.left + r.width / 2.f, r.top + 202.f);
                window.draw(cp);

                const int nb = 28;
                float baseY = r.top + 248.f;
                for (int b = 0; b < nb; ++b)
                {
                    float bh = 3.f;
                    if (playing)
                    {
                        std::size_t off = static_cast<std::size_t>(dsSounds[idx].getPlayingOffset().asSeconds() * SAMPLE_RATE);
                        float v = rmsAt(d.pcm, off + static_cast<std::size_t>(b) * 1100, 700) * d.norm;
                        bh = 3.f + 34.f * std::clamp(v, 0.f, 1.f);
                    }
                    sf::RectangleShape bar(sf::Vector2f(7.f, bh));
                    bar.setOrigin(3.5f, bh / 2.f);
                    bar.setPosition(r.left + 21.5f + b * 11.f, baseY);
                    bar.setFillColor(col(40 + b * 4, 200 - b * 3, 210, playing ? 220 : 90));
                    window.draw(bar);
                }
            }

            hint.setString("Click an image to listen    ESC: back    M: mute");
            centerText(hint, W / 2.f, 715.f);
            window.draw(hint);
        }

        window.display();
    }

    return 0;
}