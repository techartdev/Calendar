# Simple Calendar App for Flipper Zero

![Screenshot](screenshots/ss0.png)

This repository contains the source code for a calendar application designed for the Flipper Zero device. The app provides a monthly calendar grid with optional Workweek (WW) numbers, multi-calendar (.ics) holiday tracking, configurable date formats, multilingual support, and persistent settings.

## Features

- **Monthly View:** Display the days of the month with a compact 3x5 font and dynamic 5- or 6-row layout.
- **Settings Menu:** Hold `OK` (`Hold OK: Set`) to open settings:
  - **Language:** English, Ukrainian, Polish, German.
  - **Workweek (WW):** Toggle WW column ON or OFF.
  - **Week Start:** Monday or Sunday.
  - **Date Format:** EU (`DD.MM.YY`), ISO (`YYYY-MM-DD`), US (`MM/DD/YY`).
- **iCalendar (.ics) Support:** Automatically loads holidays from `.ics` files in `/ext/apps_data/techart_calendar/` and scrolls holiday names on the side panel.
- **Navigation:** 
  - **Left / Right:** Move between months.
  - **Up / Down:** Move between years.
  - **OK (Short Press):** Cycle through holidays of the month or return to today.
  - **OK (Long Press):** Open Settings menu.
  - **Back Button:** Exit Settings or exit application.

## Installation

1. Clone the repository to your local machine.
2. Follow the standard Flipper Zero development environment setup to build the application:
   ```bash
   ufbt
   ```
3. Transfer the built application to your Flipper Zero device:
   ```bash
   ufbt launch
   ```

## Usage

After installation, navigate to the app on your Flipper Zero device and open it. Use the device's buttons to navigate through the calendar as described in the features section.

## Contributing

Contributions are welcome! If you have suggestions for improvements or bug fixes, please feel free to fork the repository, make your changes, and submit a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

If you find this app helpful and would like to support the development of more tools and projects, consider buying me a coffee. Your support is greatly appreciated!

[![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/techartdev)

Thank you for your support!
