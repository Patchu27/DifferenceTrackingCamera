#include <SFML/Graphics.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <optional>
#include <math.h>

//from static tracker
int sign(auto input){
    return (input>0) - (input<0);
}

float absFloat(float input1, float input2){
    input1 = input1-input2;
    return input1*(1.f - 2.f*(input1<0));
}


float getLuminance(const sf::Color& color_){
    return ((float)(color_.r+color_.g+color_.b))/(765.f);
}

float getLumDifference(const sf::Color& color1, const sf::Color& color2){
    return absFloat(getLuminance(color1),getLuminance(color2));
}


sf::Vector3f getHue(const sf::Color& color){
    return sf::Vector3f(color.r/255.f, color.g/255.f, color.b/255.f);
}

float getHueDifference(const sf::Color& color1, const sf::Color& color2){
    return sf::Vector3f(getHue(color1)-getHue(color2)).length()/3.f;
}
//from static tracker

int clamp(int input, int min, int max){
    if (input < min) return min;
        else
        if (input > max) return max;
            
    return input;
}

float clamp(float input, float min, float max){
    if (input < min) return min;
        else
        if (input > max) return max;
            
    return input;
}

// Pass the GStreamer appsink element (cast to GstAppSink*)
// 1. Removed 'const' so the image can be efficiently moved out of the function
sf::Image captureFrameToSfImage(GstAppSink* appsink,sf::Image& inputImage) {

    // 1. Pull a sample from the appsink pipeline
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    
    if (sample) {
        // 2. Fetch width and height metadata from the frame caps
        GstCaps* caps = gst_sample_get_caps(sample);
        GstStructure* structure = gst_caps_get_structure(caps, 0);
        
        int width = 0, height = 0;
        gst_structure_get_int(structure, "width", &width);
        gst_structure_get_int(structure, "height", &height);

        // 3. Map the frame buffer memory to safely read pixel data
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            sf::Vector2u size(static_cast<unsigned int>(width), static_cast<unsigned int>(height));
            
            // Populate the sf::Image directly from the mapped memory block
            inputImage.resize(size, reinterpret_cast<const std::uint8_t*>(map.data));
            
            // Always unmap memory when done reading
            gst_buffer_unmap(buffer, &map);
        } else {
            std::cerr << "Failed to map GStreamer buffer memory!\n";
            // If mapping fails, fall back to the blue box
            //img.resize({64, 64}, sf::Color::Blue);
        }

        // 4. Safely release the sample reference ONLY if it exists
        gst_sample_unref(sample);
    } 
    else {
        // Fallback: If no frame was pulled, return the 64x64 blue block
        //img.resize({64, 64}, sf::Color::Blue);
    }

    return inputImage;
}

float getColorDifference(const sf::Color& color1, const sf::Color& color2){
    return (abs((int)color1.r-(int)color2.r),abs((int)color1.g-(int)color2.g),abs((int)color1.b-(int)color2.b))/765.f;
}

int main(){

    sf::RenderWindow window(sf::VideoMode({1920, 1080}), "DifferenceTrackingCamera", sf::State::Fullscreen);
    window.setFramerateLimit(30);
    window.setVerticalSyncEnabled(1);

// 1. Initialize GStreamer 
gst_init(nullptr, nullptr);

// 2. Build the camera capture pipeline string (Windows/macOS/Linux friendly)
// Note the format=RGBA flag at the end
sf::Vector2u outputRes = {1920,1080};

bool YUYVmode = 1;
    GstElement *pipeline = nullptr;
    if (YUYVmode)
    // Build pipeline: camera → convert → appsink
    //pipeline = gst_parse_launch("v4l2src device=/dev/video0 ! videoconvert ! video/x-raw,format=RGBA ! appsink name=sink", nullptr);
 pipeline = gst_parse_launch(
    "v4l2src device=/dev/video0 !"
    "image/jpeg,format=YUYV,width=1920,height=1080,framerate=30/1 ! "
    "jpegdec ! videoconvert ! video/x-raw,format=RGBA ! "
    "appsink name=mysink"

, nullptr);

    else    
    pipeline = gst_parse_launch(
    "v4l2src device=/dev/video0 ! "
    "image/jpeg,format=MJPG,width=1920,height=1080,framerate=30/1 ! "
    "jpegdec ! videoconvert ! video/x-raw,format=RGBA ! "
    "appsink name=mysink",
    nullptr);
// 3. Extract the appsink pointer from the pipeline
GstElement* sinkElement = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
gst_app_sink_set_max_buffers (GST_APP_SINK(sinkElement), 1);//this ensures only the most up to date frame is polled and previous ones get discarded to avoid backups
GstAppSink* appsink = GST_APP_SINK(sinkElement);

// 4. Set pipeline state to PLAYING
gst_element_set_state(pipeline, GST_STATE_PLAYING);


sf::Image rawImage{outputRes};

sf::Image prevRawImage{outputRes};

sf::Image displayImage{outputRes};

sf::Texture displayTexture(sf::Image({64,64},sf::Color::White));

sf::Sprite displaySprite(displayTexture);


float noiseDifferenceThreshold = 0.06f;//0.05
float minimumPixelPercent = 0.02f;

unsigned rightMostX = outputRes.x-1;
unsigned leftMostX = 0;
unsigned bottomMostY = outputRes.y-1;
unsigned topMostY = 0;

unsigned boxSizeX = rightMostX - leftMostX;
unsigned boxSizeY = bottomMostY- topMostY;

unsigned skipAmount = 1;//2

size_t totalPixels;
unsigned averageX = 0;
unsigned averageY = 0;

unsigned totalX = 0;
unsigned totalY = 0;

unsigned prevAverageX = 0;
unsigned prevAverageY = 0;

int prevMoveX = 0;
int prevMoveY = 0;

sf::Color keyedColor;

float luminanceThreshold = 0.1f;
float hueThreshold = 0.1f;

bool predictiveMove = 0;

sf::RectangleShape rect;
rect.setFillColor(sf::Color::Transparent);
rect.setOutlineColor(sf::Color::Green);
rect.setOutlineThickness(5.f);
// --- Inside your Main SFML 3.0 Window Loop ---
while (window.isOpen()) {
    // ... poll events ...

        // SFML 3 Event Polling Mechanics (std::optional variant standard)
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
  

        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) window.close();

        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)){
            if (displaySprite.getGlobalBounds().contains(static_cast<sf::Vector2f>(sf::Mouse::getPosition(window))))              
                keyedColor = rawImage.getPixel(static_cast<sf::Vector2u>(static_cast<sf::Vector2f>(sf::Mouse::getPosition(window)).componentWiseMul(displaySprite.getScale())));            
            }

        hueThreshold = clamp(hueThreshold + 0.01f*(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::K)-sf::Keyboard::isKeyPressed(sf::Keyboard::Key::J)),0.f,1.f);
        luminanceThreshold = clamp(luminanceThreshold + 0.01f*(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::M)-sf::Keyboard::isKeyPressed(sf::Keyboard::Key::N)),0.f,3.f);

        captureFrameToSfImage(appsink,rawImage);
        // You successfully got the sf::Image!
        // To draw it, you can upload it to an sf::Texture


        displayImage = rawImage;

        totalPixels = 0;

        totalX = 0;
        totalY = 0;

        sf::Color tempCurrent;
        sf::Color tempPrev;

        float keyLuminance = getLuminance(keyedColor);

        for (unsigned b = topMostY; b < bottomMostY; b+=skipAmount){
        for (unsigned a = leftMostX; a < rightMostX; a+=skipAmount){
            tempCurrent = rawImage.getPixel({a,b});
            tempPrev = prevRawImage.getPixel({a,b});
            if ((absFloat(getLuminance(tempCurrent),getLuminance(keyedColor))<=luminanceThreshold) && (getHueDifference(tempCurrent,keyedColor)<=hueThreshold)){
                displayImage.setPixel({a,b},sf::Color::Yellow);         
            if ((getColorDifference(tempCurrent,tempPrev) > noiseDifferenceThreshold)){
                displayImage.setPixel({a,b},sf::Color::Red);
                totalPixels++;
                totalX += a;
                totalY += b;                
                //std::cout << "yes\n";        
                if (a > rightMostX) rightMostX = a; else if (a < leftMostX) leftMostX = a;
                if (b > bottomMostY) bottomMostY = b; else if (b < topMostY) topMostY = b; 
                
            }
            }
        }
        }

        boxSizeX = clamp((rightMostX - leftMostX),250,99999);
        boxSizeY = clamp((bottomMostY- topMostY),250,99999);

        //std::cout << totalPixels << std::endl;

        if (totalPixels/(float(boxSizeX*boxSizeY*skipAmount))>=minimumPixelPercent){
        averageX = totalX/(float)totalPixels;
        averageY = totalY/(float)totalPixels;
        }
        

        

        leftMostX = clamp(averageX - boxSizeX/2,0,outputRes.x);
        rightMostX = clamp(averageX + boxSizeX/2,0,outputRes.x);
        topMostY = clamp(averageY - boxSizeY/2,0,outputRes.y);
        bottomMostY = clamp(averageY + boxSizeY/2,0,outputRes.y);

        prevMoveX = ((int)averageX - (int)prevAverageX);
        prevMoveY = ((int)averageY - (int)prevAverageY);

        sf::Vector2i moveVec = (sf::Vector2i{int(-prevMoveX),int(prevMoveY)})*2;//square POSITION from center not moveVec
        //std::cout << moveVec.x;
        sf::Mouse::setPosition(sf::Mouse::getPosition()+moveVec);

        //std::cout << leftMostX << " " << rightMostX << " " << topMostY << " " << bottomMostY << std::endl;

        if (predictiveMove){
        leftMostX = clamp((int)leftMostX + prevMoveX,0,outputRes.x);
        rightMostX = clamp((int)rightMostX + prevMoveX,0,outputRes.x);
        topMostY = clamp((int)topMostY + prevMoveY,0,outputRes.y);
        bottomMostY = clamp((int)bottomMostY + prevMoveY,0,outputRes.y);
        }

        rect.setPosition({(float)leftMostX,(float)topMostY});
        rect.setSize({(float)boxSizeX,(float)boxSizeY});
    
        prevAverageX = averageX;
        prevAverageY = averageY;


        prevRawImage = rawImage;        

        if (displayTexture.loadFromImage(displayImage));
        displaySprite.setTexture(displayTexture,1);
            
            window.clear();
            window.draw(displaySprite);
            window.draw(rect);

            window.display();
        
}

// Cleanup when closing
gst_element_set_state(pipeline, GST_STATE_NULL);
gst_object_unref(pipeline);

    return 0;

}
