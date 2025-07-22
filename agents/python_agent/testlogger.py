import keyboard

def on_key(event):
    print(f"Key: {event.name}")

keyboard.hook(on_key)
keyboard.wait('esc')  # Press ESC to exit
