#include <iostream>
#include <string>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include <sndfile.h>
#include <cmath>
#include <vector>
#include <iomanip>

// Function prototypes
cv::Mat processImage(const std::string& filePath, cv::Mat& alphaChannel);
void generateWavFile(const std::string& outputFilePath, const cv::Mat& image, const cv::Mat& alphaChannel);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <image_file>" << std::endl;
        return 1;
    }
    
    if (std::string(argv[1]).find(".png") == std::string::npos) { // Check for png extension
        std::cerr << "Error: " << argv[1] << " is not a PNG image." << std::endl;
        return 1;
    }

    std::cout << "Welcome to SoundCanvas!" << std::endl;

    std::string imageFilePath = argv[1];
    cv::Mat alphaChannel;
    cv::Mat processedImage = processImage(imageFilePath, alphaChannel);

    if (processedImage.empty() || alphaChannel.empty()) {
        return 1;
    }

    // Generate output WAV file path
    std::filesystem::path imagePath(imageFilePath);
    std::string outputWavFilePath = imagePath.stem().string() + ".wav";

    generateWavFile(outputWavFilePath, processedImage, alphaChannel);

    std::cout << "File Output: " << outputWavFilePath << std::endl;
    return 0;
}

cv::Mat processImage(const std::string& filePath, cv::Mat& alphaChannel) {
    std::cout << "Processing image..." << std::endl;

    // Read the image using OpenCV
    cv::Mat image = cv::imread(filePath, cv::IMREAD_UNCHANGED); // Ensure the alpha channel is preserved
    if (image.empty()) {
        std::cerr << "Error: Could not open or find the image." << std::endl;
        return cv::Mat();
    }

    // Check if the image has 4 channels (including alpha)
    if (image.channels() != 4) {
        std::cerr << "Error: Image does not have 4 channels (including alpha)." << std::endl;
        return cv::Mat();
    }

    // Separate the alpha channel
    std::vector<cv::Mat> channels(4);
    cv::split(image, channels);
    alphaChannel = channels[3];

    if (alphaChannel.empty()) {
        std::cerr << "Error: Alpha channel is empty." << std::endl;
        return cv::Mat();
    }

    // Process the image converting it to grayscale
    cv::Mat grayImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);

    if (grayImage.empty()) {
        std::cerr << "Error: Grayscale image is empty." << std::endl;
        return cv::Mat();
    }

    // Rotate the image and alpha channel 90 degrees counterclockwise
    cv::Mat rotatedImage, rotatedAlpha;
    cv::rotate(grayImage, rotatedImage, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(alphaChannel, rotatedAlpha, cv::ROTATE_90_COUNTERCLOCKWISE);

    // Flip the image and alpha channel vertically and horizontally to correct the mirroring effect
    cv::flip(rotatedImage, rotatedImage, 0);
    cv::flip(rotatedImage, rotatedImage, 1);
    cv::flip(rotatedAlpha, rotatedAlpha, 0);
    cv::flip(rotatedAlpha, rotatedAlpha, 1);

    if (rotatedImage.empty() || rotatedAlpha.empty()) {
        std::cerr << "Error: Rotated image or alpha channel is empty." << std::endl;
        return cv::Mat();
    }

    // Update the alpha channel with the processed version
    alphaChannel = rotatedAlpha;

    std::cout << "Image processed successfully." << std::endl;

    return rotatedImage;
}

void generateWavFile(const std::string& outputFilePath, const cv::Mat& image, const cv::Mat& alphaChannel) {
    std::cout << "Generating WAV file..." << std::endl;

    if (image.empty() || alphaChannel.empty()) {
        std::cerr << "Error: No image data to convert to WAV." << std::endl;
        return;
    }

    // Ensure the alpha channel has the same dimensions as the image
    if (image.size() != alphaChannel.size()) {
        std::cerr << "Error: Image and alpha channel dimensions do not match." << std::endl;
        return;
    }

    // Define WAV file parameters
    SF_INFO sfInfo;
    sfInfo.channels = 1;
    sfInfo.samplerate = 44100;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    // Open the WAV file for writing
    SNDFILE* outFile = sf_open(outputFilePath.c_str(), SFM_WRITE, &sfInfo);
    if (!outFile) {
        std::cerr << "Error: Could not open output WAV file." << std::endl;
        return;
    }

    // Convert image data to audio data
    std::vector<short> audioData;
    int sampleRate = sfInfo.samplerate;
    int samplesPerRow = sampleRate / 10; // Reduce the number of samples per row to shorten the duration

    double minFrequency = 200.0; // in Hz
    double maxFrequency = 8000.0; // in Hz
    double frequencyRange = maxFrequency - minFrequency;

    for (int row = 0; row < image.rows; ++row) {
        for (int i = 0; i < samplesPerRow; ++i) {
            double t = static_cast<double>(i + row * samplesPerRow) / sampleRate;
            double sampleValue = 0.0;

            for (int col = 0; col < image.cols; ++col) {
                double frequency = minFrequency + (frequencyRange * col / (image.cols - 1)); // Map column to frequency
                double intensity = static_cast<double>(image.at<uchar>(row, col)) / 255.0; // Grayscale intensity
                double alpha = static_cast<double>(alphaChannel.at<uchar>(row, col)) / 255.0; // Alpha channel

                double amplitude = alpha < 0.1 ? 0.1 : alpha;
                sampleValue += intensity * amplitude * sin(2.0 * CV_PI * frequency * t);
            }

            sampleValue = std::clamp(sampleValue, -1.0, 1.0);
            audioData.push_back(static_cast<short>(sampleValue * 32767));
        }

        // Output progress every 10 rows
        if (row % 10 == 0) {
            double progress = (static_cast<double>(row) / image.rows) * 100.0;
            std::cout << "Progress: " << std::fixed << std::setprecision(2) << progress << "%" << std::endl;
        }
    }

    const short silenceThreshold = 500;

    // Identify the start and end of the non-silent parts
    size_t start = 0;
    while (start < audioData.size() && std::abs(audioData[start]) < silenceThreshold) {
        ++start;
    }

    size_t end = audioData.size();
    while (end > start && std::abs(audioData[end - 1]) < silenceThreshold) {
        --end;
    }

    // Trim the audio data to include only the non-silent parts
    std::vector<short> trimmedAudioData(audioData.begin() + start, audioData.begin() + end);

    // Write trimmed audio data to the WAV file
    sf_write_short(outFile, trimmedAudioData.data(), trimmedAudioData.size());

    // Close the WAV file
    sf_close(outFile);

    std::cout << "WAV file generated successfully." << std::endl;
}
