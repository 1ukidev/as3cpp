package game {
    import flash.display.Sprite;
    import flash.geom.Point;

    /**
     * The character controlled by the user.
     */
    public class Player extends Sprite {
        private var _position:Point = new Point();

        /** Remaining lives. */
        public var lives:int = 3;

        public function Player(name:String) {
            this.name = name;
        }

        /**
         * Moves the player to a new position.
         *
         * @param x The horizontal position.
         * @param y The vertical position.
         */
        public function moveTo(x:Number, y:Number):void {
            _position.setTo(x, y);
            this.x = x;
            this.y = y;
        }

        public function get position():Point {
            return _position.clone();
        }
    }
}
