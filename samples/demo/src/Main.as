package {
    import flash.display.Sprite;
    import flash.events.Event;

    import game.Player;

    public class Main extends Sprite {
        private var player:Player;

        public function Main() {
            addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
        }

        private function onAddedToStage(event:Event):void {
            player = new Player("Hero");
            player.moveTo(stage.stageWidth / 2, stage.stageHeight / 2);
            addChild(player);
            trace(player.name, player.position);
        }
    }
}
