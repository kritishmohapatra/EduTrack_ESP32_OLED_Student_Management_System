#include <FS.h>            // File System library - Provides basic file system operations.
#include <SPIFFS.h>        // SPI Flash File System library - Specific library for managing files on the ESP32's built-in flash memory.
#include <ArduinoJson.h>   // JSON library - Used for parsing and generating JSON data, specifically for student records.
#include <Wire.h>          // I2C library - Enables communication with I2C devices like the OLED display.
#include <Adafruit_SH1106.h> // SH1106 OLED Library - Specific library for controlling SH1106-based OLED displays.

#define SCREEN_WIDTH 128     // Defines the width of the OLED screen in pixels.
#define SCREEN_HEIGHT 64     // Defines the height of the OLED screen in pixels.
#define OLED_RESET   -1      // Specifies the reset pin for the OLED. -1 means it shares the Arduino's reset pin or is not used.

Adafruit_SH1106 display(OLED_RESET); // Creates an object to control the OLED display.

// Structure to hold information for each student.
struct Student {
  String regdNo;  // Student's registration number (e.g., "S001").
  String name;    // Student's name (e.g., "John Doe").
  float gpa;      // Student's Grade Point Average (e.g., 3.85).
  String branch;  // Student's academic branch (e.g., "CSE").
};

Student students[1000]; // An array to store up to 1000 Student objects in memory.
int currentStudentIndex = 0; // Tracks the index of the student currently selected or at the top of the display.
int studentCount = 0;        // Keeps a dynamic count of how many students are currently loaded.
const int studentsToShow = 2; // Defines how many student names are displayed at a time on the OLED in the list view.

// setup() function: Runs once when the ESP32 starts up.
void setup() {
  Serial.begin(115200); // Initializes serial communication for debugging and receiving commands from the Serial Monitor.

  // Initialize SPIFFS (SPI Flash File System).
  // The 'true' argument tells SPIFFS to format the file system if it fails to mount for the first time.
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed"); // Print error to Serial Monitor if mount fails.
    // Display error message on the OLED.
    display.clearDisplay();    // Clear any previous content on the OLED.
    display.setTextSize(1);    // Set text size to 1 (smallest).
    display.setTextColor(WHITE); // Set text color to white.
    display.setCursor(0,0);    // Set cursor to the top-left corner.
    display.println("SPIFFS Mount Failed!"); // Write the message to the display buffer.
    display.display();         // Show the content on the OLED.
    return; // Halt execution if SPIFFS fails, as file operations are critical.
  }
  Serial.println("SPIFFS Mounted Successfully"); // Confirm successful SPIFFS mount.

  // Initialize the SH1106 OLED display.
  // SH1106_SWITCHCAPVCC specifies the internal charge pump for OLED power.
  // 0x3C is the common I2C address for SH1106 OLEDs.
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); // Clear the display buffer.
  display.display();      // Update the physical display to be blank.

  loadStudentsFromJson(); // Call function to load student data from "students.json" file.
  displayStudentNames();  // Call function to display the initial set of student names on the OLED.
}

// loop() function: Runs continuously after setup() completes.
void loop() {
  // Check if there is any data available to read from the Serial Monitor.
  if (Serial.available() > 0) {
    String command = Serial.readString(); // Read the entire incoming string from Serial.
    command.trim();                       // Remove any leading or trailing whitespace (like newline characters).

    Serial.print("Received command: "); // Echo the command received for debugging purposes.
    Serial.println(command);

    // --- START OF LOGIC FOR COMMAND HANDLING ---
    // The 'add' command is handled first, always allowed.
    if (command.startsWith("add")) {
      addStudent(command);      // Call the function to add a new student.
      printJsonFile();          // Print the current content of the JSON file (for debugging).
      return; // Exit loop() early after processing 'add' to avoid further command checks for this iteration.
    }

    // For all other commands ('ok', 'back', 'up', 'down', 'edit'),
    // check if there are any students loaded. If not, display an error.
    if (studentCount == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(0,0);
      display.println("No students. Use 'add' command."); // Inform user on OLED.
      display.display();
      Serial.println("No students loaded. Only 'add' command is allowed."); // Inform user on Serial.
      return; // Stop processing other commands if no students are present.
    }

    // Process other commands if students exist.
    if (command == "ok") {
      showFullData(currentStudentIndex); // Display full details of the currently selected student.
    }
    else if (command == "back") {
      displayStudentNames(); // Return to the student name list display.
    }
    else if (command == "down") {
      // Move to the next student in the list.
      // '% studentCount' ensures wrapping around to the beginning if at the end of the list.
      currentStudentIndex = (currentStudentIndex + 1) % studentCount;
      displayStudentNames(); // Refresh the OLED with the new selection.
    }
    else if (command == "up") {
      // Move to the previous student in the list.
      // '+ studentCount' before '%' handles negative results from subtraction correctly for wrapping.
      currentStudentIndex = (currentStudentIndex - 1 + studentCount) % studentCount;
      displayStudentNames(); // Refresh the OLED with the new selection.
    }
    else if (command.startsWith("edit")) {
      editStudentData(command); // Call the function to edit an existing student's details.
    }
    // The 'add' command is intentionally handled at the very top, so no need to check here again.
    // --- END OF LOGIC FOR COMMAND HANDLING ---
  }
}

// loadStudentsFromJson() function: Reads student data from "students.json" file into the 'students' array.
void loadStudentsFromJson() {
  File file = SPIFFS.open("/students.json", "r"); // Open the "students.json" file in read mode.
  if (!file) { // Check if the file failed to open (e.g., file doesn't exist).
    Serial.println("Failed to open file for reading or file does not exist. Initializing with no students.");
    studentCount = 0; // If file not found or opened, assume no students.
    return;
  }

  // Read the entire content of the file into a String to check if it's empty.
  String fileContent = file.readString();
  file.close(); // Close the file immediately after reading.

  if (fileContent.length() == 0) { // Check if the file content is empty.
    Serial.println("students.json is empty. Initializing with no students.");
    studentCount = 0; // If file is empty, set student count to 0.
    return;
  }

  DynamicJsonDocument doc(8192); // Create a DynamicJsonDocument. 8192 bytes (8KB) is a common capacity, adjust if needed for more students.
                               // DynamicJsonDocument allocates memory on the heap as needed, suitable for varying JSON sizes.
  // Attempt to parse the JSON content from the String.
  DeserializationError error = deserializeJson(doc, fileContent);
  
  if (error) { // Check if there was an error during JSON parsing.
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str()); // Print the specific error message.
    studentCount = 0; // If parsing fails, assume no valid student data.
    // Display error on OLED.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("JSON Parse Failed!");
    display.display();
    return;
  }

  studentCount = doc.size(); // Get the number of elements (students) from the JSON array.
  Serial.print("Loaded ");
  Serial.print(studentCount);
  Serial.println(" students from JSON.");
  printJsonFile(); // For debugging, display the content that was just loaded from the JSON file.
  
  // Iterate through the JSON array and populate the 'students' array.
  // The loop runs for the number of students found or up to the maximum array capacity (1000).
  for (int i = 0; i < studentCount && i < 1000; i++) {
    students[i].regdNo = doc[i]["regdNo"].as<String>();    // Extract "regdNo" and convert to String.
    students[i].name = doc[i]["name"].as<String>();        // Extract "name" and convert to String.
    students[i].gpa = doc[i]["gpa"].as<float>();          // Extract "gpa" and convert to float.
    students[i].branch = doc[i]["branch"].as<String>();    // Extract "branch" and convert to String.
  }
}

// displayStudentNames() function: Displays two student names and registration numbers on the OLED.
void displayStudentNames() {
  display.clearDisplay();    // Clear the OLED display buffer.
  display.setTextSize(1);    // Set text size to 1.
  display.setTextColor(WHITE); // Set text color to white.

  if (studentCount == 0) { // If no students are loaded, display a message.
    display.setCursor(0, 0);
    display.println("No students found.");
    display.println("Use 'add' command to add students.");
    display.display();
    return; // Exit the function.
  }

  int largeBorderHeight = 30; // Height for the border of the currently selected student.
  // int smallBorderHeight = 24; // This variable is declared but not used in the final border drawing.
  int borderThickness = 2;    // Thickness of the borders around the student information.

  int startStudentIndex = currentStudentIndex; // The index of the first student to display (the selected one).
  // Calculate the index of the second student to display.
  // '% studentCount' ensures it wraps around if the first student is the last one in the list.
  int secondStudentIndex = (currentStudentIndex + 1) % studentCount;  

  // Draw a larger border for the currently selected student.
  for (int i = 0; i < borderThickness; i++) {
    display.drawRect(i, 1 + i, SCREEN_WIDTH - 2 * i, largeBorderHeight, WHITE); // Draw a rectangle.
  }

  // Display details for the first student (selected).
  display.setCursor(10, 8); // Position cursor for the name.
  display.print("Name: ");
  display.println(students[startStudentIndex].name);
  display.setCursor(10, 18); // Position cursor for the registration number.
  display.print("Regd No: ");
  display.println(students[startStudentIndex].regdNo);

  // Only display a second student if there's more than one student in total.
  if (studentCount > 1) {
    // Draw a border for the second student (not selected, just next in list).
    display.drawRect(0, 34, SCREEN_WIDTH, 30, WHITE); // Draw a rectangle.
    display.setCursor(10, 43); // Position cursor for the name.
    display.print("Name: ");
    display.println(students[secondStudentIndex].name);
    display.setCursor(10, 53); // Position cursor for the registration number.
    display.print("Regd No: ");
    display.println(students[secondStudentIndex].regdNo);
  } else {
    // If there's only one student, indicate the end of the list.
    display.setCursor(10, 43);
    display.println("-- End of List --");
  }

  display.display(); // Push the content of the buffer to the actual OLED screen.
}

// showFullData() function: Displays all details of a specific student on the OLED.
void showFullData(int index) {
  display.clearDisplay(); // Clear the OLED buffer.
  display.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, WHITE); // Draw an outer border around the display area.
  display.setTextSize(1);    // Set text size.
  display.setTextColor(WHITE); // Set text color.

  // Display each piece of student information, one line at a time.
  display.setCursor(10, 10);
  display.print("Regd No: ");
  display.println(students[index].regdNo);
  display.setCursor(10, 20);
  display.print("Name: ");
  display.println(students[index].name);
  display.setCursor(10, 30);
  display.print("GPA: ");
  display.println(students[index].gpa);
  display.setCursor(10, 40);
  display.print("Branch: ");
  display.println(students[index].branch);

  display.display(); // Update the physical OLED display.
}

// editStudentData() function: Edits an existing student's details based on their old registration number.
// Expected command format: "edit <oldRegdNo> <newRegdNo> <newName> <newGPA> <newBranch>"
void editStudentData(String command) {
  // Extract the part of the command that contains the student data.
  // It starts after the first space (i.e., after "edit").
  String data = command.substring(command.indexOf(' ') + 1);
  int space1 = data.indexOf(' ');         // Find the space separating oldRegdNo and newRegdNo.
  int space2 = data.indexOf(' ', space1 + 1); // Find the space separating newRegdNo and newName.
  int space3 = data.indexOf(' ', space2 + 1); // Find the space separating newName and newGPA.
  int space4 = data.indexOf(' ', space3 + 1); // Find the space separating newGPA and newBranch.

  // Check if all necessary parts of the command (delimited by spaces) are present.
  if (space1 != -1 && space2 != -1 && space3 != -1 && space4 != -1) {
    String oldRegdNo = data.substring(0, space1);             // Extract the old registration number.
    String newRegdNo = data.substring(space1 + 1, space2);     // Extract the new registration number.
    String newName = data.substring(space2 + 1, space3);       // Extract the new name.
    float newGPA = data.substring(space3 + 1, space4).toFloat(); // Extract the new GPA and convert it to a float.
    String newBranch = data.substring(space4 + 1);            // Extract the new branch (the rest of the string).

    bool studentFound = false; // Flag to indicate if the student to edit was found.
    // Loop through all currently loaded students to find a match by oldRegdNo.
    for (int i = 0; i < studentCount; i++) {
      if (students[i].regdNo == oldRegdNo) { // If the old registration number matches.
        students[i].regdNo = newRegdNo;   // Update student's registration number.
        students[i].name = newName;       // Update student's name.
        students[i].gpa = newGPA;         // Update student's GPA.
        students[i].branch = newBranch;   // Update student's branch.
        studentFound = true;              // Set flag to true as student is found and updated.
        break;                            // Exit the loop as the student has been found and modified.
      }
    }

    if (studentFound) {
      saveStudentsToJson(); // Save the updated student data back to the "students.json" file.
      Serial.println("Student data updated successfully."); // Confirm success on Serial.
      displayStudentNames(); // Refresh the OLED display to show any changes.
    } else {
      Serial.println("Student with given RegdNo not found!"); // Inform user on Serial if no student matched.
      // Display error on OLED.
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(0,0);
      display.println("Student not found!");
      display.display();
      delay(2000); // Show the error message on OLED for 2 seconds.
      displayStudentNames(); // Return to the names display after the delay.
    }
  } else {
    Serial.println("Invalid edit command format"); // Inform user about incorrect command syntax on Serial.
    // Display error on OLED.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Invalid edit format!");
    display.display();
    delay(2000); // Show the error message on OLED for 2 seconds.
    displayStudentNames(); // Return to the names display after the delay.
  }
}

// addStudent() function: Adds a new student record to the 'students' array and saves it to JSON.
// Expected command format: "add <regdNo> <name> <gpa> <branch>"
void addStudent(String command) {
  // Check if the student array has reached its maximum capacity.
  if (studentCount >= 1000) {
    Serial.println("Max student limit reached"); // Inform user on Serial.
    // Display error on OLED.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Max students reached!");
    display.display();
    delay(2000); // Show the error message for 2 seconds.
    displayStudentNames(); // Return to the names display.
    return; // Exit the function if the limit is reached.
  }

  // Extract the part of the command that contains the new student's data.
  String data = command.substring(command.indexOf(' ') + 1);
  int space1 = data.indexOf(' ');         // Find the space separating regdNo and name.
  int space2 = data.indexOf(' ', space1 + 1); // Find the space separating name and gpa.
  int space3 = data.indexOf(' ', space2 + 1); // Find the space separating gpa and branch.

  // Check if all necessary parts of the command are present.
  if (space1 != -1 && space2 != -1 && space3 != -1) {
    students[studentCount].regdNo = data.substring(0, space1);             // Extract registration number.
    students[studentCount].name = data.substring(space1 + 1, space2);       // Extract name.
    students[studentCount].gpa = data.substring(space2 + 1, space3).toFloat(); // Extract GPA and convert to float.
    students[studentCount].branch = data.substring(space3 + 1);            // Extract branch (rest of the string).
    studentCount++; // Increment the total count of students.
    saveStudentsToJson(); // Save the updated list of students to the JSON file.
    displayStudentNames(); // Refresh the OLED display to show the newly added student.
    Serial.println("Student added successfully!"); // Confirm success on Serial.
  } else {
    Serial.println("Invalid add command format"); // Inform user about incorrect command syntax on Serial.
    // Display error on OLED.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Invalid add format!");
    display.display();
    delay(2000); // Show the error message for 2 seconds.
    displayStudentNames(); // Return to the names display.
  }
}

// saveStudentsToJson() function: Saves the current 'students' array content to "students.json" file.
void saveStudentsToJson() {
  DynamicJsonDocument doc(8192); // Create a DynamicJsonDocument for serialization. 8KB capacity.

  // Iterate through all students in the 'students' array and add them as JSON objects to the document.
  for (int i = 0; i < studentCount; i++) {
    JsonObject student = doc.createNestedObject(); // Create a new JSON object for each student.
    student["regdNo"] = students[i].regdNo;    // Add registration number to the JSON object.
    student["name"] = students[i].name;        // Add name to the JSON object.
    student["gpa"] = students[i].gpa;          // Add GPA to the JSON object.
    student["branch"] = students[i].branch;    // Add branch to the JSON object.
  }

  // Open the "students.json" file in write mode.
  // If the file exists, its content will be truncated; otherwise, a new file will be created.
  File file = SPIFFS.open("/students.json", "w");
  if (!file) { // Check if the file failed to open for writing.
    Serial.println("Failed to open file for writing!"); // Inform user on Serial.
    // Display error on OLED.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("File write failed!");
    display.display();
    return; // Exit function if file cannot be opened.
  }

  serializeJson(doc, file); // Serialize the JSON document (all student data) to the file.
  file.close();             // Close the file to ensure all data is written and resources are released.

  Serial.println("Student data saved successfully!"); // Confirm successful save on Serial.
}

// printJsonFile() function: Reads and prints the entire content of "students.json" to the Serial Monitor.
// Useful for debugging and verifying file contents.
void printJsonFile() {
  File file = SPIFFS.open("/students.json", "r"); // Open the "students.json" file in read mode.
  if (!file) { // Check if the file failed to open.
    Serial.println("Failed to open file for reading!"); // Inform user on Serial.
    return; // Exit function if file cannot be opened.
  }

  Serial.println("\nCurrent JSON Data in SPIFFS:"); // Header for the output.
  while (file.available()) { // Loop as long as there are bytes available to read in the file.
    Serial.write(file.read()); // Read each byte and print it to the Serial Monitor.
  }
  Serial.println(); // Add a newline at the end for cleaner output in Serial Monitor.
  file.close();     // Close the file.
}