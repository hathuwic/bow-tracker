import tensorflow as tf
from tensorflow import keras
import numpy as np

class BowTracker():
    def __init__(self, model_path):

        self._MODEL_PATH = model_path
        self.model = tf.keras.models.load_model(self._MODEL_PATH)

    def _get_force(hsd: float) -> float:
        pass

    def predict(self, sensor_values: tuple[float, float, float, float]) -> list[float, float]:
        sensor_values = np.array(sensor_values).reshape((1,4))
        prediction = self.model(sensor_values).numpy()[0]
        return prediction.tolist()