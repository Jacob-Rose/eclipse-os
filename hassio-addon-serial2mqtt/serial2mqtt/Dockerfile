ARG BUILD_FROM
FROM $BUILD_FROM

# Install Python and dependencies
RUN apk add --no-cache \
    python3 \
    py3-pip \
    py3-setuptools

# Install Python packages
RUN pip3 install --no-cache-dir \
    pyserial \
    paho-mqtt

# Copy the run script
COPY run.py /run.py
RUN chmod +x /run.py

# Run the bridge
CMD ["python3", "/run.py"]
