import os

def get_root_dir():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def get_output_dir():
    root = get_root_dir()
    output = os.path.join(root, "output")
    if not os.path.exists(output):
        try:
            os.makedirs(output)
        except Exception:
            pass
    return output

def get_lib_dir():
    root = get_root_dir()
    lib_dir = os.path.join(root, "build", "lib")
    return lib_dir

if __name__ == "__main__":
    print(f"Root: {get_root_dir()}")
    print(f"Output: {get_output_dir()}")
    print(f"Lib: {get_lib_dir()}")