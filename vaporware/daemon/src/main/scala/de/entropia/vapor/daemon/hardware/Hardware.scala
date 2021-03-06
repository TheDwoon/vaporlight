package de.entropia.vapor.hardware

import de.entropia.vapor.util._
import de.entropia.vapor.daemon.config.Settings
import com.typesafe.scalalogging.slf4j.Logging


/**
 * Facade for the rest of the package.
 */
class Hardware(val mapping: Mapping) extends Logging {

  /** Update a LED. Won't take effect until `strobe()` is called. */
  def set(led: Int, color: Color) = {
    logger.debug("set(%d, %s)".format(led, color))
    mapping.set(led, color)
  }

  /** Make previous LED state updates take effect. */
  def strobe() =
    mapping.strobe()
}


object Hardware {

  def apply(settings: Settings) =
    new Hardware(
      new Mapping(settings,
        new Buffer(settings,
          new Encoder(
            new Framer(
              Physical.open(settings))))))
}
