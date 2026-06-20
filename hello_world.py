"""A minimal Tkinter GUI app that displays 'HELLO WORLD!' centered in a window."""

import tkinter as tk


def main():
    # The root window — the top-level container for all widgets.
    root = tk.Tk()
    root.title("Hello World")
    root.geometry("400x300")  # width x height in pixels

    # A Label widget holding the text we want to display.
    label = tk.Label(root, text="HELLO WORLD!", font=("Arial", 24))

    # pack(expand=True) lets the label fill the window and centers it.
    label.pack(expand=True)

    # Start the event loop — keeps the window open and responsive.
    root.mainloop()


if __name__ == "__main__":
    # Closing the window with the X exits cleanly. Catching KeyboardInterrupt
    # means pressing Stop / Ctrl+C also exits quietly instead of dumping a traceback.
    try:
        main()
    except KeyboardInterrupt:
        pass
