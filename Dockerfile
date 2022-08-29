FROM gitpod/workspace-full-vnc
RUN sudo apt-get update && \
    sudo apt-get install -y libgtk-3-dev && \
    sudo rm -rf /var/lib/apt/lists/*

RUN pyenv install 3.9.13
RUN pyenv global 3.9.13
COPY requirements.txt requirements.txt
RUN python -m pip install -r requirements.txt
