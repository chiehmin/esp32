<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->

# ESP32 Development Guidelines

This is an ESP32 project using the ESP-IDF framework. When working with this codebase:

## Code Style
- Use ESP-IDF coding standards and conventions
- Include comprehensive documentation for all functions
- Use ESP_LOG macros for logging instead of printf
- Follow FreeRTOS task management best practices

## Hardware Considerations
- Default LED pin is GPIO2 (built-in LED on most ESP32 boards)
- Default button pin is GPIO0 (boot button on most ESP32 boards)
- Always configure GPIO pins properly before use
- Consider power consumption in task delays

## ESP-IDF Specific
- Use esp_err_t return types for error handling
- Prefer ESP-IDF APIs over standard C library when available
- Use FreeRTOS primitives for task synchronization
- Include proper error checking for all ESP-IDF function calls

## Project Structure
- main.c contains the main application logic
- Keep hardware-specific code modular and well-documented
- Use meaningful task names and appropriate stack sizes
- Document pin assignments and hardware connections
