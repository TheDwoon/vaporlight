package de.entropia.vapor.server

import java.net.InetSocketAddress
import org.jboss.netty.bootstrap.ServerBootstrap
import org.jboss.netty.channel.socket.nio.NioServerSocketChannelFactory
import org.jboss.netty.buffer.ChannelBuffer
import org.jboss.netty.channel._
import org.jboss.netty.channel.group.{DefaultChannelGroup, ChannelGroup}
import java.util.concurrent.Executors
import org.jboss.netty.handler.codec.frame.FrameDecoder
import grizzled.slf4j.Logging
import de.entropia.vapor.mixer.Mixer
import de.entropia.vapor.daemon.config.Settings
import de.entropia.vapor.daemon.mixer.Manager


sealed abstract class ServerMsg()

case class ServerStartMsg() extends ServerMsg()

case class ServerQuitMsg() extends ServerMsg()


/**
 * A netty-based vaporlight server.
 */
class Server(val settings: Settings, val mixer: Mixer, val manager: Manager) {

  val channels: ChannelGroup = new DefaultChannelGroup
  val clients: ChannelLocal[Client] = new ChannelLocal[Client](false)
  var factory = new NioServerSocketChannelFactory(
    Executors.newCachedThreadPool,
    Executors.newCachedThreadPool)

  def start() {
    val bootstrap = new ServerBootstrap(factory)
    bootstrap.setOption("reuseAddress", true)
    bootstrap.setOption("keepAlive", true)
    bootstrap.setOption("tcpNoDelay", true)
    bootstrap.setPipelineFactory(new ChannelPipelineFactory {
      def getPipeline = Channels.pipeline(
        new VaporlightFrameDecoder(),
        new VaporlightChannelHandler(channels, clients, settings, mixer, manager))
    })
    channels.add(bootstrap.bind(new InetSocketAddress(7534)))
  }

  def quit() {
    channels.close.awaitUninterruptibly
    factory.releaseExternalResources()
  }
}

/**
 * Takes bytes from a bytestream and converts them to messages.
 *
 * Can be inserted into a netty pipeline.
 */
class VaporlightFrameDecoder() extends FrameDecoder with Logging {
  var nextOp: Option[MessageType] = None

  def decode(ctx: ChannelHandlerContext, channel: Channel, buffer: ChannelBuffer) = {
    if (nextOp.isEmpty && buffer.readableBytes() >= 1) {
      nextOp = Option(Message.lookup(buffer.readByte()))
    }
    if (nextOp.isDefined && buffer.readableBytes() >= nextOp.get.payloadLength) {
      val payload = buffer.readBytes(nextOp.get.payloadLength).toByteBuffer.array
      val result = nextOp.get.parse(payload)
      nextOp = Option.empty
      result
    } else {
      null // not enought data to reassemble an entire command yet
    }
  }
}

/**
 * Handles a single connection.
 */
class VaporlightChannelHandler(val channels: ChannelGroup, val local: ChannelLocal[Client], val settings: Settings, val mixer: Mixer, val manager: Manager) extends SimpleChannelUpstreamHandler with Logging {
  info("channel created")

  override def handleUpstream(ctx: ChannelHandlerContext, e: org.jboss.netty.channel.ChannelEvent) {
    e match {
      case c: ChannelStateEvent => logger.info(e.toString)
      case _ =>
    }
    super.handleUpstream(ctx, e)
  }

  override def channelConnected(ctx: ChannelHandlerContext, e: ChannelStateEvent) {
    channels.add(e.getChannel)
    local.set(ctx.getChannel, new Client(settings, mixer, manager))
    local.get(ctx.getChannel).connect()
  }

  override def channelClosed(ctx: ChannelHandlerContext, e: ChannelStateEvent) {
    val client = local.get(ctx.getChannel)
    local.remove(ctx.getChannel)
    try {
      client.disconnect()
    } catch {
      case e: Exception =>
        debug(e)
    }
  }

  override def messageReceived(ctx: ChannelHandlerContext, e: org.jboss.netty.channel.MessageEvent) {
    val m = e.getMessage.asInstanceOf[Message] // generated by frame decoder in pipeline
    try {
      local.get(ctx.getChannel).dispatch(m)
    } catch {
      case e: Exception =>
        debug("client threw exception; client killed", e)
        ctx.getChannel.close()
    }
  }

  override def exceptionCaught(ctx: ChannelHandlerContext, e: ExceptionEvent) {
    error("unexpected exception from downstream", e.getCause)
    local.get(ctx.getChannel).disconnect()
    ctx.getChannel.close()
    local.remove(ctx.getChannel)
  }
}


sealed class ChannelEvent()

case object ConnectEvent extends ChannelEvent

case object DisconnectEvent extends ChannelEvent

case class MessageEvent(m: Message) extends ChannelEvent
