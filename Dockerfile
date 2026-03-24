FROM gcc:14.3

WORKDIR /app

COPY . .

CMD ["bash"]
