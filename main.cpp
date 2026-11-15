#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cctype>
#include <map>

namespace fs = std::filesystem;
using namespace std;

constexpr float W = 1200.f;
constexpr float H = 750.f;
constexpr int MAX_IMAGES = 6;

const unsigned int SAMPLE_RATE = 44100;
const float MIN_FREQUENCY = 220.0f;
const float MAX_FREQUENCY = 880.0f;
const float MIN_AMPLITUDE = 0.08f;
const float MAX_AMPLITUDE = 0.35f;
const int MAX_PIXELS = 256;
const float PIXEL_DURATION = 0.025f;

// ---------------------------------------------------------------
// SOUND FUNCTIONS
// ---------------------------------------------------------------
float normalizeValue(float value, float minValue, float maxValue) {
    if (maxValue <= minValue) return 0.0f;
    return (value - minValue) / (maxValue - minValue);
}

float getBrightness(sf::Color pixel) {
    return (0.299f * pixel.r + 0.587f * pixel.g + 0.114f * pixel.b);
}

float getSaturation(sf::Color pixel) {
    float maximum = max({pixel.r, pixel.g, pixel.b});
    float minimum = min({pixel.r, pixel.g, pixel.b});
    if (maximum == 0) return 0.0f;
    return (maximum - minimum) / maximum;
}

float calculateFrequency(float brightness) {
    float normalized = normalizeValue(brightness, 0.0f, 255.0f);
    return MIN_FREQUENCY + normalized * (MAX_FREQUENCY - MIN_FREQUENCY);
}

float calculateAmplitude(float saturation) {
    return MIN_AMPLITUDE + saturation * (MAX_AMPLITUDE - MIN_AMPLITUDE);
}

vector<sf::Color> reducePixels(const vector<sf::Color>& pixels) {
    vector<sf::Color> reduced;
    if (pixels.empty()) return reduced;
    int totalPixels = pixels.size();
    int newSize = min(totalPixels, MAX_PIXELS);
    for (int i = 0; i < newSize; i++) {
        int start = i * totalPixels / newSize;
        int end = (i + 1) * totalPixels / newSize;
        int red = 0;
        int green = 0;
        int blue = 0;
        int count = end - start;
        for (int j = start; j < end; j++) {
            red += pixels[j].r;
            green += pixels[j].g;
            blue += pixels[j].b;
        }
        reduced.push_back(sf::Color( red / count, green / count, blue / count ));
    }
    return reduced;
}

void generateSound(const vector<vector<sf::Color>>& images, vector<sf::Int16>& samples) {
    vector<float> soundData;
    float previousFrequency = MIN_FREQUENCY;
    float previousAmplitude = MIN_AMPLITUDE;
    const int samplesPerPixel = static_cast<int>(SAMPLE_RATE * PIXEL_DURATION);
    float phase = 0.0f;
    for (int i = 0; i < images.size(); i++) {
        vector<sf::Color> pixels = reducePixels(images[i]);
        for (int j = 0; j < pixels.size(); j++) {
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
                phase += 2.0f * 3.14159265f * currentFrequency / SAMPLE_RATE;
                if (phase >= 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
            }
            previousFrequency = frequency;
            previousAmplitude = amplitude;
        }
    }
    int fadeSamples = SAMPLE_RATE * 0.02f;
    for (int i = 0; i < soundData.size(); i++) {
        float fade = 1.0f;
        if (i < fadeSamples) {
            fade = static_cast<float>(i) / fadeSamples;
        }
        if (i >= soundData.size() - fadeSamples) {
            float fadeOut = static_cast<float>(soundData.size() - i) / fadeSamples;
            fade = min(fade, fadeOut);
        }
        float finalSample = soundData[i] * fade;
        finalSample = max(-1.0f, min(1.0f, finalSample));
        samples.push_back( static_cast<sf::Int16>(finalSample * 32767) );
    }
}

void playSound(const vector<sf::Int16>& samples) {
    if (samples.empty()) {
        cout << "No sound data available!" << endl;
        return;
    }
    sf::SoundBuffer soundBuffer;
    if (!soundBuffer.loadFromSamples( samples.data(), samples.size(), 1, SAMPLE_RATE)) {
        cout << "Failed to create sound buffer!" << endl;
        return;
    }
    sf::Sound sound(soundBuffer);
    sound.play();
    while (sound.getStatus() == sf::Sound::Playing) {
        sf::sleep(sf::milliseconds(100));
    }
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

// ---------------------------------------------------------------
// AUDIO BACKEND BRIDGE STUCTURE
// ---------------------------------------------------------------
struct PlaybackState
{
    bool  playing = false;
    float progress = 0.f;
    float level = 0.f;
    std::vector<float> wave;
};

struct IAudioBackend
{
    virtual ~IAudioBackend() = default;
    virtual void toggleImage(std::size_t index) = 0;
    virtual void stopAll() = 0;
    virtual void setMuted(bool muted) = 0;
    virtual void onUiEvent(const std::string& name) = 0;
    virtual PlaybackState state(std::size_t index) = 0;
};

class SoundAudioBackend : public IAudioBackend
{
private:
    std::vector<sf::SoundBuffer> buffers;
    std::vector<sf::Sound> sounds;
    std::vector<std::vector<sf::Int16>> pcmData;
    bool muted = false;
    int playingIndex = -1;

public:
    SoundAudioBackend(const vector<vector<sf::Color>>& imagesPixels)
    {
        buffers.reserve(imagesPixels.size());
        sounds.reserve(imagesPixels.size());
        pcmData.reserve(imagesPixels.size());

        for (const auto& imgPix : imagesPixels)
        {
            vector<vector<sf::Color>> singleWrap = { imgPix };
            vector<sf::Int16> samples;
            generateSound(singleWrap, samples);
            pcmData.push_back(samples);

            sf::SoundBuffer buf;
            if (!samples.empty())
            {
                buf.loadFromSamples(samples.data(), samples.size(), 1, SAMPLE_RATE);
            }
            buffers.push_back(buf);
            sounds.emplace_back(buffers.back());
            sounds.back().setVolume(90.f);
        }
    }

    void toggleImage(std::size_t index) override
    {
        stopAll();
        if (index < sounds.size())
        {
            playingIndex = static_cast<int>(index);
            sounds[index].setVolume(muted ? 0.f : 90.f);
            sounds[index].play();
        }
    }

    void stopAll() override
    {
        for (auto& sound : sounds)
        {
            sound.stop();
        }
        playingIndex = -1;
    }

    void setMuted(bool m) override
    {
        muted = m;
        for (auto& sound : sounds)
        {
            sound.setVolume(muted ? 0.f : 90.f);
        }
    }

    void onUiEvent(const std::string&) override {}

    PlaybackState state(std::size_t index) override
    {
        PlaybackState st;
        if (index < sounds.size())
        {
            st.playing = (sounds[index].getStatus() == sf::Sound::Playing);
            if (st.playing)
            {
                float duration = static_cast<float>(pcmData[index].size()) / SAMPLE_RATE;
                float offset = sounds[index].getPlayingOffset().asSeconds();
                st.progress = (duration > 0.f) ? (offset / duration) : 0.f;

                std::size_t off = static_cast<std::size_t>(offset * SAMPLE_RATE);
                st.level = rmsAt(pcmData[index], off, 4096);

                st.wave.clear();
                for (int b = 0; b < 22; ++b)
                {
                    float v = rmsAt(pcmData[index], off + static_cast<std::size_t>(b) * 1100, 700);
                    st.wave.push_back(std::clamp(v, 0.f, 1.f));
                }
            }
        }
        return st;
    }
};

// ---------------------------------------------------------------
// GUI HELPERS & SETUP
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

static void fitText(sf::Text& t, float maxW, unsigned minSize = 12)
{
    while (t.getLocalBounds().width > maxW && t.getCharacterSize() > minSize)
        t.setCharacterSize(t.getCharacterSize() - 1);
}

static std::string wrapText(const std::string& s, const sf::Font& font, unsigned size, float maxW)
{
    std::string out, line, word;
    std::istringstream iss(s);
    while (iss >> word)
    {
        std::string test = line.empty() ? word : line + " " + word;
        sf::Text t(test, font, size);
        if (t.getLocalBounds().width > maxW && !line.empty())
        {
            out += line + "\n";
            line = word;
        }
        else
        {
            line = test;
        }
    }
    out += line;
    return out;
}

static void drawCover(sf::RenderWindow& window, const sf::Texture& tex, sf::Vector2f pos, sf::Vector2f size)
{
    auto ts = tex.getSize();
    float ta = static_cast<float>(ts.x) / static_cast<float>(ts.y);
    float ba = size.x / size.y;
    int tw = static_cast<int>(ts.x), th = static_cast<int>(ts.y), ox = 0, oy = 0;
    if (ta > ba) { tw = static_cast<int>(ts.y * ba); ox = (static_cast<int>(ts.x) - tw) / 2; }
    else { th = static_cast<int>(ts.x / ba); oy = (static_cast<int>(ts.y) - th) / 2; }

    sf::Sprite spr(tex);
    spr.setTextureRect(sf::IntRect(ox, oy, tw, th));
    spr.setPosition(pos);
    spr.setScale(size.x / static_cast<float>(tw), size.y / static_cast<float>(th));
    window.draw(spr);
}

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string prettyName(const std::string& stem)
{
    std::string out = stem;
    bool newWord = true;
    for (char& c : out)
    {
        if (c == '_' || c == '-') c = ' ';
        if (newWord && std::isalpha(static_cast<unsigned char>(c)))
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        newWord = (c == ' ');
    }
    return out;
}

static sf::Color tempColor(float v, int a)
{
    struct Stop { float p; int r, g, b; };
    static const Stop st[] = { { 0.f, 25, 80, 220 },{ 0.3f, 30, 200, 170 },{ 0.55f, 120, 230, 70 },
                             { 0.8f, 250, 210, 60 },{ 1.f, 255, 120, 40 } };
    v = std::clamp(v, 0.f, 1.f);
    for (int i = 0; i < 4; ++i)
        if (v <= st[i + 1].p)
        {
            float f = (v - st[i].p) / (st[i + 1].p - st[i].p);
            return col(int(st[i].r + (st[i + 1].r - st[i].r) * f),
                int(st[i].g + (st[i + 1].g - st[i].g) * f),
                int(st[i].b + (st[i + 1].b - st[i].b) * f), a);
        }
    return col(255, 120, 40, a);
}

struct ImageInfo { const char* name; const char* caption; };

static const std::map<std::string, ImageInfo>& imageInfoMap()
{
    static const std::map<std::string, ImageInfo> info = {
        { "image_1",{ "Ocean Currents", "A full-resolution map of ocean currents from the ECCO2 model. White dots mark where the model sets the current direction; values in between are interpolated." } },
        { "image_2",{ "Water Flows", "A cropped region of the Pacific showing the real flow data points (white dots) against the flow curves. Directions between the dots are interpolated." } },
        { "image_3",{ "Surface Eddies", "A cropped region of the Atlantic with the same flow data points. The bright spot near the center comes from a corner of the cube-sphere grid the data uses." } },
        { "image_4",{ "Heat Map", "Sea surface currents and temperatures across the world's oceans, cropped and diced for a 5x3 hyperwall display." } },
        { "image_5",{ "Ocean Motion", "Sea surface currents shown as flowing curves, colored by temperature." } },
        { "image_6",{ "Atlantic Current", "Sea surface currents of the northern Atlantic Ocean, produced for a 5x3 hyperwall." } }
    };
    return info;
}

static const std::vector<ImageInfo>& imageInfoOrder()
{
    static const std::vector<ImageInfo> order = []
        {
            std::vector<ImageInfo> v;
            for (const auto& kv : imageInfoMap()) v.push_back(kv.second);
            return v;
        }();
    return order;
}

static bool loadBackground(const std::string& stem, sf::Texture& tex)
{
    const char* dirs[] = { "assets", "../assets", "../../assets", "../../../assets" };
    const char* exts[] = { ".jpg", ".jpeg", ".png" };
    for (const char* d : dirs)
        for (const char* e : exts)
        {
            fs::path p = fs::path(d) / (stem + e);
            std::error_code ec;
            if (fs::is_regular_file(p, ec) && tex.loadFromFile(p.string()))
            {
                tex.setSmooth(true);
                return true;
            }
        }
    return false;
}

struct FlowP { float x, y, age, life, tv; };
struct Plankton { float x, y, r, phase, twinkle, drift; };

struct Dataset
{
    std::string name;
    std::string fileName;
    std::string desc;
    sf::Texture tex;
};

enum class Screen { Start, Workspace, Gallery };

int main()
{
    // WINDOW
    sf::RenderWindow window(sf::VideoMode(1200u, 750u), "Space Walkie Talkie");
    window.setFramerateLimit(60);

    // FONT
    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/arial.ttf"))
    {
        std::cout << "Font loading failed!\n";
        return -1;
    }

    // ---------------------------------------------------------------
    // DATA INPUT
    // ---------------------------------------------------------------
    vector < vector < sf::Color > > ImagesPixels;
    int indexCounter = 1;
    sf::Image image;
    while(indexCounter < MAX_IMAGES)
    {
        string filePath = "Data/Image_" + to_string(indexCounter) + ".png";
        if (!image.loadFromFile(filePath)) {
            filePath = "Data/Image_" + to_string(indexCounter) + ".jpg";
            if (!image.loadFromFile(filePath)) {
                indexCounter++;
                continue;
            }
        }
        unsigned int width = image.getSize().x;
        unsigned int height = image.getSize().y;
        vector < sf::Color> tempPixels;
        tempPixels.reserve(width * height);
        for (unsigned int y = 0 ; y < height ; ++y) {
            for (unsigned int x = 0 ; x < width ; ++x) {
                tempPixels.push_back(image.getPixel(x, y));
            }
        }
        ImagesPixels.push_back(tempPixels);
        indexCounter++;
    }

    // BACKEND INITIALIZATION
    SoundAudioBackend soundBackend(ImagesPixels);
    IAudioBackend* audio = &soundBackend;
    bool muted = false;

    // LOADING DATASETS
    std::vector<Dataset> datasets;
    for (size_t idx = 0; idx < ImagesPixels.size(); ++idx)
    {
        int fileIdx = static_cast<int>(idx + 1);
        string fileNameStr = "image_" + to_string(fileIdx);

        Dataset d;
        d.fileName = "Image_" + to_string(fileIdx) + ".png";
        d.name = prettyName(fileNameStr);

        auto it = imageInfoMap().find(fileNameStr);
        if (it != imageInfoMap().end())
        {
            d.name = it->second.name;
            d.desc = it->second.caption;
        }

        string path = "Data/Image_" + to_string(fileIdx) + ".png";
        sf::Image tempImg;
        if (!tempImg.loadFromFile(path)) {
            path = "Data/Image_" + to_string(fileIdx) + ".jpg";
            tempImg.loadFromFile(path);
        }
        d.tex.loadFromImage(tempImg);
        d.tex.setSmooth(true);

        datasets.push_back(d);
    }

    // BACKGROUND
    sf::Texture bgStartTex, bgWelcomeTex;
    bool hasBgStart = loadBackground("bg_start", bgStartTex);
    bool hasBgWelcome = loadBackground("bg_welcome", bgWelcomeTex);

    std::vector<PlaybackState> pbs(datasets.size());
    std::vector<float> smooth(datasets.size(), 0.f);
    int selected = -1;

    // RANDOM
    std::mt19937 rng(std::random_device{}());
    auto rnd = [&](float a, float b)
        {
            return std::uniform_real_distribution<float>(a, b)(rng);
        };

    // OCEAN SCENE DATA
    std::vector<Plankton> plankton;
    for (int i = 0; i < 120; ++i)
        plankton.push_back({ rnd(0, W), rnd(0, H), rnd(0.8f, 2.2f), rnd(0, 6.28f), rnd(1.f, 3.f), rnd(4.f, 16.f) });

    sf::RenderTexture flowRT;
    flowRT.create(1200, 750);
    flowRT.clear(sf::Color::Black);
    flowRT.display();
    std::vector<FlowP> fp(1500);
    auto respawn = [&](FlowP& p)
        {
            p.x = rnd(0, W); p.y = rnd(0, H);
            p.age = 0.f; p.life = rnd(2.f, 6.f); p.tv = rnd(-0.15f, 0.15f);
        };
    for (auto& p : fp) { respawn(p); p.age = rnd(0.f, p.life); }
    sf::VertexArray flowLines(sf::Lines);

    float t = 0.f;

    sf::VertexArray bg(sf::TriangleStrip, 4);
    bg[0].position = sf::Vector2f(0.f, 0.f);
    bg[1].position = sf::Vector2f(W, 0.f);
    bg[2].position = sf::Vector2f(0.f, H);
    bg[3].position = sf::Vector2f(W, H);

    sf::CircleShape dot(1.f, 24);

    // START SCREEN UI
    sf::Text title;
    title.setFont(font);
    title.setString("Space Walkie Talkie");
    title.setCharacterSize(65);
    title.setFillColor(sf::Color(95, 224, 208));
    centerText(title, W / 2.f, 230.f);

    sf::Text subtitle;
    subtitle.setFont(font);
    subtitle.setString("Turn Earth's Data Into Sound");
    subtitle.setCharacterSize(24);
    subtitle.setFillColor(sf::Color(200, 225, 232));
    centerText(subtitle, W / 2.f, 320.f);

    const float startBtnY = hasBgStart ? H / 2.f : 450.f;
    sf::RectangleShape startButton(sf::Vector2f(260.f, 80.f));
    startButton.setOrigin(130.f, 40.f);
    startButton.setPosition(W / 2.f, startBtnY);
    startButton.setFillColor(sf::Color(20, 145, 160));
    startButton.setOutlineThickness(2.f);
    startButton.setOutlineColor(sf::Color(150, 240, 245, 160));

    sf::Text buttonText;
    buttonText.setFont(font);
    buttonText.setString("START");
    buttonText.setCharacterSize(32);
    buttonText.setFillColor(sf::Color::White);
    centerText(buttonText, W / 2.f, startBtnY);

    // WELCOME UI
    sf::Text welcome;
    welcome.setFont(font);
    welcome.setString("WELCOME TO YOUR WORKSPACE");
    welcome.setCharacterSize(38);
    welcome.setFillColor(sf::Color::White);
    centerText(welcome, W / 2.f, 95.f);

    const float cardCX = W / 2.f;
    const float cardCY = 560.f;
    const sf::Vector2f cardSize(360.f, 120.f);

    sf::Text hint;
    hint.setFont(font);
    hint.setString("ESC: back    M: mute");
    hint.setCharacterSize(16);
    hint.setFillColor(sf::Color(160, 180, 195));
    centerText(hint, W / 2.f, 722.f);

    auto nasaCardRect = [&]()
        {
            return sf::FloatRect(cardCX - cardSize.x / 2.f, cardCY - cardSize.y / 2.f, cardSize.x, cardSize.y);
        };

    // GALLERY LAYOUT
    const float CARD_W = 265.f, CARD_H = 285.f, GAP = 25.f;
    auto galleryRect = [&](int i)
        {
            int n = static_cast<int>(datasets.size());
            int perRow = 4;
            int row = i / perRow, c = i % perRow;
            int inRow = (row == 0) ? std::min(perRow, n) : n - perRow;
            float totalW = inRow * CARD_W + (inRow - 1) * GAP;
            float startX = (W - totalW) / 2.f;
            return sf::FloatRect(startX + c * (CARD_W + GAP), 100.f + row * 300.f, CARD_W, CARD_H);
        };

    Screen screen = Screen::Start;
    sf::Clock clock;

    // MAIN LOOP
    while (window.isOpen())
    {
        float dt = std::min(clock.restart().asSeconds(), 0.05f);
        t += dt;

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                audio->stopAll();
                window.close();
            }

            if (event.type == sf::Event::KeyPressed)
            {
                if (event.key.code == sf::Keyboard::Escape)
                {
                    if (screen == Screen::Gallery && selected >= 0) { audio->stopAll(); selected = -1; }
                    else if (screen == Screen::Gallery) { audio->stopAll(); screen = Screen::Workspace; }
                    else if (screen == Screen::Workspace) { screen = Screen::Start; }
                }
                if (event.key.code == sf::Keyboard::M)
                {
                    muted = !muted;
                    audio->setMuted(muted);
                }
            }

            if (event.type == sf::Event::MouseButtonPressed)
            {
                if (event.mouseButton.button == sf::Mouse::Left)
                {
                    sf::Vector2f position(static_cast<float>(event.mouseButton.x),
                        static_cast<float>(event.mouseButton.y));

                    if (screen == Screen::Start && startButton.getGlobalBounds().contains(position))
                    {
                        screen = Screen::Workspace;
                        audio->onUiEvent("start");
                    }
                    else if (screen == Screen::Workspace)
                    {
                        if (nasaCardRect().contains(position))
                        {
                            audio->onUiEvent("open_gallery");
                            screen = Screen::Gallery;
                        }
                    }
                    else if (screen == Screen::Gallery)
                    {
                        if (selected >= 0)
                        {
                            audio->stopAll();
                            selected = -1;
                        }
                        else
                        {
                            for (std::size_t i = 0; i < datasets.size(); ++i)
                            {
                                if (galleryRect(static_cast<int>(i)).contains(position))
                                {
                                    selected = static_cast<int>(i);
                                    audio->toggleImage(i);
                                    smooth[i] = 0.f;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // UPDATE
        for (auto& s : plankton)
        {
            s.y -= s.drift * dt;
            s.x += std::sin(t * 0.5f + s.phase) * 6.f * dt;
            if (s.y < -5.f) { s.y = H + 5.f; s.x = rnd(0, W); }
        }

        {
            sf::RectangleShape fade(sf::Vector2f(W, H));
            fade.setFillColor(col(0, 0, 0, 16));
            flowRT.draw(fade);
            flowLines.clear();
            for (auto& p : fp)
            {
                float ang = std::sin(p.x * 0.006f + t * 0.15f) * 2.2f
                    + std::cos(p.y * 0.008f - t * 0.10f) * 2.2f
                    + std::sin((p.x + p.y) * 0.004f + t * 0.07f) * 1.5f;
                float nx = p.x + (std::cos(ang) * 70.f + 25.f) * dt;
                float ny = p.y + std::sin(ang) * 70.f * dt;
                float life01 = p.age / p.life;
                int alpha = std::max(0, int(230.f * std::sin(3.14159265f * std::min(life01, 1.f))));
                sf::Color c = tempColor(1.f - std::fabs(2.f * p.y / H - 1.f) + p.tv, alpha);
                flowLines.append(sf::Vertex(sf::Vector2f(p.x, p.y), c));
                flowLines.append(sf::Vertex(sf::Vector2f(nx, ny), c));
                p.x = nx; p.y = ny; p.age += dt;
                if (p.age > p.life || nx < -5.f || nx > W + 5.f || ny < -5.f || ny > H + 5.f) respawn(p);
            }
            flowRT.draw(flowLines);
            flowRT.display();
        }

        for (std::size_t i = 0; i < datasets.size(); ++i)
        {
            pbs[i] = audio->state(i);
            float target = pbs[i].playing ? std::clamp(pbs[i].level, 0.f, 1.f) : 0.f;
            smooth[i] += (target - smooth[i]) * std::min(1.f, dt * 10.f);
        }

        auto mousePixel = sf::Mouse::getPosition(window);
        sf::Vector2f mousePosition(static_cast<float>(mousePixel.x), static_cast<float>(mousePixel.y));

        bool hover = screen == Screen::Start && startButton.getGlobalBounds().contains(mousePosition);
        startButton.setFillColor(hover ? sf::Color(40, 190, 195) : sf::Color(20, 145, 160));
        startButton.setScale(hover ? sf::Vector2f(1.05f, 1.05f) : sf::Vector2f(1.f, 1.f));

        // DRAW
        window.clear();

        auto glow = [&](float x, float y, float r, sf::Color c)
            {
                for (int i = 0; i < 5; ++i)
                {
                    float rr = r * (1.f - i * 0.18f);
                    sf::CircleShape g(rr, 64);
                    g.setOrigin(rr, rr);
                    g.setPosition(x, y);
                    g.setFillColor(col(c.r, c.g, c.b, 10));
                    window.draw(g);
                }
            };

        bool photoBg = (screen == Screen::Start && hasBgStart) || (screen == Screen::Workspace && hasBgWelcome);
        if (photoBg && screen == Screen::Start)
        {
            sf::RectangleShape base(sf::Vector2f(W, H));
            base.setFillColor(col(28, 30, 34));
            window.draw(base);

            auto ts = bgStartTex.getSize();
            float sc = std::min(W / static_cast<float>(ts.x), H / static_cast<float>(ts.y));
            sf::Sprite spr(bgStartTex);
            spr.setScale(sc, sc);
            spr.setPosition((W - ts.x * sc) / 2.f, (H - ts.y * sc) / 2.f);
            window.draw(spr);
        }
        else if (photoBg)
        {
            drawCover(window, bgWelcomeTex, sf::Vector2f(0.f, 0.f), sf::Vector2f(W, H));
            sf::RectangleShape shade(sf::Vector2f(W, H));
            shade.setFillColor(col(0, 10, 25, 120));
            window.draw(shade);
        }
        else
        {
            float k = 0.5f + 0.5f * std::sin(t * 0.2f);
            sf::Color top, bottom;
            if (screen == Screen::Start)
            {
                top = col(6, 24, 48);
                bottom = col(10, int(80 + 24 * k), int(104 + 10 * k));
            }
            else if (screen == Screen::Workspace)
            {
                top = col(8, 30, 56);
                bottom = col(14, int(100 + 20 * k), int(122 + 8 * k));
            }
            else
            {
                top = col(4, 16, 32);
                bottom = col(6, int(40 + 10 * k), int(62 + 8 * k));
            }
            bg[0].color = top; bg[1].color = top;
            bg[2].color = bottom; bg[3].color = bottom;
            window.draw(bg);

            if (screen == Screen::Start)
            {
                glow(300.f + 60.f * std::sin(t * 0.2f), 200.f, 280.f, col(40, 200, 200));
                glow(920.f + 50.f * std::cos(t * 0.17f), 560.f, 300.f, col(30, 120, 210));
            }
            else if (screen == Screen::Workspace)
            {
                glow(W / 2.f + 80.f * std::sin(t * 0.15f), H + 60.f, 420.f, col(255, 154, 92));
                glow(200.f, 120.f, 260.f, col(40, 200, 200));
            }
            else
            {
                glow(1000.f + 40.f * std::sin(t * 0.18f), 120.f, 300.f, col(40, 200, 200));
            }

            sf::Sprite flowSpr(flowRT.getTexture());
            flowSpr.setColor(col(255, 255, 255, screen == Screen::Gallery ? 90 : 255));
            sf::RenderStates add;
            add.blendMode = sf::BlendAdd;
            window.draw(flowSpr, add);
        }

        if (!(screen == Screen::Start && hasBgStart))
            for (const auto& s : plankton)
            {
                float a = 0.5f + 0.5f * std::sin(t * s.twinkle + s.phase);
                dot.setRadius(s.r);
                dot.setOrigin(s.r, s.r);
                dot.setPosition(s.x, s.y);
                dot.setFillColor(col(190, 245, 240, int(40 + 110 * a)));
                window.draw(dot);
            }

        // SCREENS
        if (screen == Screen::Start)
        {
            if (!hasBgStart)
            {
                centerText(title, W / 2.f, 230.f + 6.f * std::sin(t * 1.5f));
                window.draw(title);
                window.draw(subtitle);
            }
            window.draw(startButton);
            window.draw(buttonText);
        }
        else if (screen == Screen::Workspace)
        {
            window.draw(welcome);

            const int barCount = 56;
            for (int i = 0; i < barCount; ++i)
            {
                float wave = (0.5f + 0.5f * std::sin(t * 3.f + i * 0.35f))
                    * (0.6f + 0.4f * std::sin(t * 1.3f + i * 0.12f));
                float h = 14.f + 80.f * wave;
                sf::RectangleShape bar(sf::Vector2f(10.f, h));
                bar.setOrigin(5.f, h / 2.f);
                bar.setPosition(208.f + i * 14.f, 400.f);
                bar.setFillColor(col(40 + i * 2, 190 - i, 200, int(120 + 120 * wave)));
                window.draw(bar);
            }

            bool cardHover = nasaCardRect().contains(mousePosition);

            sf::RectangleShape card(cardSize);
            card.setOrigin(cardSize.x / 2.f, cardSize.y / 2.f);
            card.setPosition(cardCX, cardCY);
            card.setFillColor(col(255, 255, 255, cardHover ? 40 : 18));
            card.setOutlineThickness(cardHover ? 2.5f : 1.5f);
            card.setOutlineColor(col(95, 224, 208, cardHover ? 255 : 140));
            window.draw(card);

            sf::Text ct; ct.setFont(font); ct.setString("NASA DATA"); ct.setCharacterSize(28);
            ct.setFillColor(sf::Color(95, 224, 208));
            centerText(ct, cardCX, cardCY - 15.f);
            window.draw(ct);

            sf::Text ci; ci.setFont(font); ci.setString("Click to explore"); ci.setCharacterSize(18);
            ci.setFillColor(sf::Color(210, 228, 235));
            centerText(ci, cardCX, cardCY + 25.f);
            window.draw(ci);

            hint.setString("ESC: back    M: mute");
            centerText(hint, W / 2.f, 722.f);
            window.draw(hint);
        }
        else // GALLERY
        {
            sf::Text gt; gt.setFont(font); gt.setString("NASA EARTH DATA"); gt.setCharacterSize(34);
            gt.setFillColor(sf::Color::White);
            centerText(gt, W / 2.f, 38.f);
            window.draw(gt);

            std::string sub = "Click any image to view it full screen";
            if (!datasets.empty() && datasets.size() < static_cast<std::size_t>(MAX_IMAGES))
                sub += "    (" + std::to_string(datasets.size()) + " of " + std::to_string(MAX_IMAGES) + " images found)";
            sf::Text gs; gs.setFont(font); gs.setString(sub); gs.setCharacterSize(17);
            gs.setFillColor(sf::Color(95, 224, 208));
            centerText(gs, W / 2.f, 72.f);
            window.draw(gs);

            if (datasets.empty())
            {
                sf::Text m1; m1.setFont(font); m1.setString("No images found"); m1.setCharacterSize(30);
                m1.setFillColor(sf::Color(255, 220, 130));
                centerText(m1, W / 2.f, 300.f);
                window.draw(m1);

                sf::Text m2;
                m2.setFont(font);
                m2.setString("Put your NASA images (.png / .jpg) in the 'Data' folder,");
                m2.setCharacterSize(20);

                sf::Text m3;
                m3.setFont(font);
                m3.setString("then build and run again.");
                m3.setCharacterSize(20);
                m2.setFillColor(sf::Color(210, 228, 235));
                m3.setFillColor(sf::Color(210, 228, 235));
                centerText(m2, W / 2.f, 360.f);
                centerText(m3, W / 2.f, 392.f);
                window.draw(m2);
                window.draw(m3);
            }

            for (std::size_t i = 0; i < datasets.size(); ++i)
            {
                Dataset& d = datasets[i];
                sf::FloatRect r = galleryRect(static_cast<int>(i));
                bool hov = r.contains(mousePosition);
                bool open = false;
                bool playing = pbs[i].playing;
                float a = smooth[i];

                sf::RectangleShape gcard(sf::Vector2f(r.width, r.height));
                gcard.setPosition(r.left, r.top);
                gcard.setFillColor(col(255, 255, 255, (hov || open || playing) ? 34 : 18));
                gcard.setOutlineThickness((open || playing) ? 2.5f : 1.5f);
                gcard.setOutlineColor((open || playing) ? col(130, 255, 230, int(170 + 85 * a))
                    : col(95, 224, 208, hov ? 220 : 120));
                window.draw(gcard);

                float ix = r.left + 10.f;
                float iy = r.top + 10.f;
                const float imgW = CARD_W - 20.f, imgH = 130.f;
                drawCover(window, d.tex, sf::Vector2f(ix, iy), sf::Vector2f(imgW, imgH));

                if (playing)
                {
                    sf::RectangleShape flash(sf::Vector2f(imgW, imgH));
                    flash.setPosition(ix, iy);
                    flash.setFillColor(col(255, 255, 255, int(60 * a)));
                    window.draw(flash);

                    float prog = std::clamp(pbs[i].progress, 0.f, 1.f);
                    sf::RectangleShape line(sf::Vector2f(2.5f, imgH));
                    line.setPosition(ix + prog * imgW, iy);
                    line.setFillColor(col(255, 255, 255, 220));
                    window.draw(line);
                }

                sf::Text nm; nm.setFont(font); nm.setString(d.name); nm.setCharacterSize(18);
                nm.setFillColor(open ? sf::Color(255, 220, 130) : sf::Color::White);
                fitText(nm, CARD_W - 20.f);
                centerText(nm, r.left + r.width / 2.f, r.top + 163.f);
                window.draw(nm);

                sf::Text cp; cp.setFont(font); cp.setString("Click to view"); cp.setCharacterSize(14);
                cp.setFillColor(col(150, 175, 190));
                cp.setPosition(r.left + 12.f, r.top + 183.f);
                window.draw(cp);

                const int nb = 22;
                float baseY = r.top + 255.f;
                float startX = r.left + (CARD_W - nb * 11.f) / 2.f + 5.5f;
                for (int b = 0; b < nb; ++b)
                {
                    float bh = 3.f;
                    if (playing)
                    {
                        float v = (static_cast<std::size_t>(b) < pbs[i].wave.size()) ? pbs[i].wave[b] : a;
                        bh = 3.f + 30.f * std::clamp(v, 0.f, 1.f);
                    }
                    sf::RectangleShape bar(sf::Vector2f(6.f, bh));
                    bar.setOrigin(3.f, bh / 2.f);
                    bar.setPosition(startX + b * 11.f, baseY);
                    bar.setFillColor(col(40 + b * 5, 200 - b * 4, 210, playing ? 220 : 90));
                    window.draw(bar);
                }
            }

            if (selected >= 0 && selected < static_cast<int>(datasets.size()))
            {
                Dataset& d = datasets[static_cast<std::size_t>(selected)];

                sf::RectangleShape dim(sf::Vector2f(W, H));
                dim.setFillColor(col(2, 10, 22, 240));
                window.draw(dim);

                sf::Text fn; fn.setFont(font); fn.setString(d.name); fn.setCharacterSize(34);
                fn.setFillColor(sf::Color(255, 220, 130));
                centerText(fn, W / 2.f, 42.f);
                window.draw(fn);

                const float maxW = W - 120.f, maxH = 520.f;
                auto ts = d.tex.getSize();
                float sc = std::min(maxW / static_cast<float>(ts.x), maxH / static_cast<float>(ts.y));
                float iw = ts.x * sc, ih = ts.y * sc;
                float ix = (W - iw) / 2.f, iy = 82.f + (maxH - ih) / 2.f;

                sf::Sprite big(d.tex);
                big.setScale(sc, sc);
                big.setPosition(ix, iy);
                window.draw(big);

                sf::RectangleShape frame(sf::Vector2f(iw, ih));
                frame.setPosition(ix, iy);
                frame.setFillColor(sf::Color::Transparent);
                frame.setOutlineThickness(2.f);
                frame.setOutlineColor(col(95, 224, 208, 200));
                window.draw(frame);

                std::string cap = d.desc.empty() ? std::string("No caption yet") : d.desc;
                sf::Text fc; fc.setFont(font); fc.setString(wrapText(cap, font, 24, W - 240.f)); fc.setCharacterSize(24);
                fc.setFillColor(col(210, 235, 240));
                centerText(fc, W / 2.f, 655.f);
                window.draw(fc);

                hint.setString("Click anywhere or press ESC to close    M: mute");
            }
            else
            {
                hint.setString("Click an image to view it    ESC: back    M: mute");
            }
            centerText(hint, W / 2.f, 722.f);
            window.draw(hint);
        }

        window.display();
    }

    return 0;
}